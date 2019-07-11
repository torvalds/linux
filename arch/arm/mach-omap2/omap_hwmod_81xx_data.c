/*
 * DM81xx hwmod data.
 *
 * Copyright (C) 2010 Texas Instruments, Inc. - http://www.ti.com/
 * Copyright (C) 2013 SKTB SKiT, http://www.skitlab.ru/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/types.h>

#include <linux/platform_data/hsmmc-omap.h>

#include "omap_hwmod_common_data.h"
#include "cm81xx.h"
#include "ti81xx.h"
#include "wd_timer.h"

/*
 * DM816X hardware modules integration data
 *
 * Note: This is incomplete and at present, not generated from h/w database.
 */

/*
 * Common alwon .clkctrl_offs from dm814x TRM "Table 2-278. CM_ALWON REGISTERS"
 * also dm816x TRM 18.7.17 CM_ALWON device register values minus 0x1400.
 */
#define DM81XX_CM_ALWON_MCASP0_CLKCTRL		0x140
#define DM81XX_CM_ALWON_MCASP1_CLKCTRL		0x144
#define DM81XX_CM_ALWON_MCASP2_CLKCTRL		0x148
#define DM81XX_CM_ALWON_MCBSP_CLKCTRL		0x14c
#define DM81XX_CM_ALWON_UART_0_CLKCTRL		0x150
#define DM81XX_CM_ALWON_UART_1_CLKCTRL		0x154
#define DM81XX_CM_ALWON_UART_2_CLKCTRL		0x158
#define DM81XX_CM_ALWON_GPIO_0_CLKCTRL		0x15c
#define DM81XX_CM_ALWON_GPIO_1_CLKCTRL		0x160
#define DM81XX_CM_ALWON_I2C_0_CLKCTRL		0x164
#define DM81XX_CM_ALWON_I2C_1_CLKCTRL		0x168
#define DM81XX_CM_ALWON_WDTIMER_CLKCTRL		0x18c
#define DM81XX_CM_ALWON_SPI_CLKCTRL		0x190
#define DM81XX_CM_ALWON_MAILBOX_CLKCTRL		0x194
#define DM81XX_CM_ALWON_SPINBOX_CLKCTRL		0x198
#define DM81XX_CM_ALWON_MMUDATA_CLKCTRL		0x19c
#define DM81XX_CM_ALWON_MMUCFG_CLKCTRL		0x1a8
#define DM81XX_CM_ALWON_CONTROL_CLKCTRL		0x1c4
#define DM81XX_CM_ALWON_GPMC_CLKCTRL		0x1d0
#define DM81XX_CM_ALWON_ETHERNET_0_CLKCTRL	0x1d4
#define DM81XX_CM_ALWON_L3_CLKCTRL		0x1e4
#define DM81XX_CM_ALWON_L4HS_CLKCTRL		0x1e8
#define DM81XX_CM_ALWON_L4LS_CLKCTRL		0x1ec
#define DM81XX_CM_ALWON_RTC_CLKCTRL		0x1f0
#define DM81XX_CM_ALWON_TPCC_CLKCTRL		0x1f4
#define DM81XX_CM_ALWON_TPTC0_CLKCTRL		0x1f8
#define DM81XX_CM_ALWON_TPTC1_CLKCTRL		0x1fc
#define DM81XX_CM_ALWON_TPTC2_CLKCTRL		0x200
#define DM81XX_CM_ALWON_TPTC3_CLKCTRL		0x204

/* Registers specific to dm814x */
#define DM814X_CM_ALWON_MCASP_3_4_5_CLKCTRL	0x16c
#define DM814X_CM_ALWON_ATL_CLKCTRL		0x170
#define DM814X_CM_ALWON_MLB_CLKCTRL		0x174
#define DM814X_CM_ALWON_PATA_CLKCTRL		0x178
#define DM814X_CM_ALWON_UART_3_CLKCTRL		0x180
#define DM814X_CM_ALWON_UART_4_CLKCTRL		0x184
#define DM814X_CM_ALWON_UART_5_CLKCTRL		0x188
#define DM814X_CM_ALWON_OCM_0_CLKCTRL		0x1b4
#define DM814X_CM_ALWON_VCP_CLKCTRL		0x1b8
#define DM814X_CM_ALWON_MPU_CLKCTRL		0x1dc
#define DM814X_CM_ALWON_DEBUGSS_CLKCTRL		0x1e0
#define DM814X_CM_ALWON_DCAN_0_1_CLKCTRL	0x218
#define DM814X_CM_ALWON_MMCHS_0_CLKCTRL		0x21c
#define DM814X_CM_ALWON_MMCHS_1_CLKCTRL		0x220
#define DM814X_CM_ALWON_MMCHS_2_CLKCTRL		0x224
#define DM814X_CM_ALWON_CUST_EFUSE_CLKCTRL	0x228

/* Registers specific to dm816x */
#define DM816X_DM_ALWON_BASE		0x1400
#define DM816X_CM_ALWON_TIMER_1_CLKCTRL	(0x1570 - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_TIMER_2_CLKCTRL	(0x1574 - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_TIMER_3_CLKCTRL	(0x1578 - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_TIMER_4_CLKCTRL	(0x157c - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_TIMER_5_CLKCTRL	(0x1580 - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_TIMER_6_CLKCTRL	(0x1584 - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_TIMER_7_CLKCTRL	(0x1588 - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_SDIO_CLKCTRL	(0x15b0 - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_OCMC_0_CLKCTRL	(0x15b4 - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_OCMC_1_CLKCTRL	(0x15b8 - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_ETHERNET_1_CLKCTRL (0x15d8 - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_MPU_CLKCTRL	(0x15dc - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_SR_0_CLKCTRL	(0x1608 - DM816X_DM_ALWON_BASE)
#define DM816X_CM_ALWON_SR_1_CLKCTRL	(0x160c - DM816X_DM_ALWON_BASE)

/*
 * The default .clkctrl_offs field is offset from CM_DEFAULT, that's
 * TRM 18.7.6 CM_DEFAULT device register values minus 0x500
 */
#define DM81XX_CM_DEFAULT_OFFSET	0x500
#define DM81XX_CM_DEFAULT_USB_CLKCTRL	(0x558 - DM81XX_CM_DEFAULT_OFFSET)
#define DM81XX_CM_DEFAULT_SATA_CLKCTRL	(0x560 - DM81XX_CM_DEFAULT_OFFSET)

/* L3 Interconnect entries clocked at 125, 250 and 500MHz */
static struct omap_hwmod dm81xx_alwon_l3_slow_hwmod = {
	.name		= "alwon_l3_slow",
	.clkdm_name	= "alwon_l3s_clkdm",
	.class		= &l3_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod dm81xx_default_l3_slow_hwmod = {
	.name		= "default_l3_slow",
	.clkdm_name	= "default_l3_slow_clkdm",
	.class		= &l3_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod dm81xx_alwon_l3_med_hwmod = {
	.name		= "l3_med",
	.clkdm_name	= "alwon_l3_med_clkdm",
	.class		= &l3_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod dm81xx_alwon_l3_fast_hwmod = {
	.name		= "l3_fast",
	.clkdm_name	= "alwon_l3_fast_clkdm",
	.class		= &l3_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

/*
 * L4 standard peripherals, see TRM table 1-12 for devices using this.
 * See TRM table 1-73 for devices using the 125MHz SYSCLK6 clock.
 */
static struct omap_hwmod dm81xx_l4_ls_hwmod = {
	.name		= "l4_ls",
	.clkdm_name	= "alwon_l3s_clkdm",
	.class		= &l4_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

/*
 * L4 high-speed peripherals. For devices using this, please see the TRM
 * table 1-13. On dm816x, only EMAC, MDIO and SATA use this. See also TRM
 * table 1-73 for devices using 250MHz SYSCLK5 clock.
 */
static struct omap_hwmod dm81xx_l4_hs_hwmod = {
	.name		= "l4_hs",
	.clkdm_name	= "alwon_l3_med_clkdm",
	.class		= &l4_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

/* L3 slow -> L4 ls peripheral interface running at 125MHz */
static struct omap_hwmod_ocp_if dm81xx_alwon_l3_slow__l4_ls = {
	.master	= &dm81xx_alwon_l3_slow_hwmod,
	.slave	= &dm81xx_l4_ls_hwmod,
	.user	= OCP_USER_MPU,
};

/* L3 med -> L4 fast peripheral interface running at 250MHz */
static struct omap_hwmod_ocp_if dm81xx_alwon_l3_slow__l4_hs = {
	.master	= &dm81xx_alwon_l3_med_hwmod,
	.slave	= &dm81xx_l4_hs_hwmod,
	.user	= OCP_USER_MPU,
};

/* MPU */
static struct omap_hwmod dm814x_mpu_hwmod = {
	.name		= "mpu",
	.clkdm_name	= "alwon_l3s_clkdm",
	.class		= &mpu_hwmod_class,
	.flags		= HWMOD_INIT_NO_IDLE,
	.main_clk	= "mpu_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM814X_CM_ALWON_MPU_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_ocp_if dm814x_mpu__alwon_l3_slow = {
	.master		= &dm814x_mpu_hwmod,
	.slave		= &dm81xx_alwon_l3_slow_hwmod,
	.user		= OCP_USER_MPU,
};

/* L3 med peripheral interface running at 200MHz */
static struct omap_hwmod_ocp_if dm814x_mpu__alwon_l3_med = {
	.master	= &dm814x_mpu_hwmod,
	.slave	= &dm81xx_alwon_l3_med_hwmod,
	.user	= OCP_USER_MPU,
};

static struct omap_hwmod dm816x_mpu_hwmod = {
	.name		= "mpu",
	.clkdm_name	= "alwon_mpu_clkdm",
	.class		= &mpu_hwmod_class,
	.flags		= HWMOD_INIT_NO_IDLE,
	.main_clk	= "mpu_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM816X_CM_ALWON_MPU_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_ocp_if dm816x_mpu__alwon_l3_slow = {
	.master		= &dm816x_mpu_hwmod,
	.slave		= &dm81xx_alwon_l3_slow_hwmod,
	.user		= OCP_USER_MPU,
};

/* L3 med peripheral interface running at 250MHz */
static struct omap_hwmod_ocp_if dm816x_mpu__alwon_l3_med = {
	.master	= &dm816x_mpu_hwmod,
	.slave	= &dm81xx_alwon_l3_med_hwmod,
	.user	= OCP_USER_MPU,
};

/* RTC */
static struct omap_hwmod_class_sysconfig ti81xx_rtc_sysc = {
	.rev_offs	= 0x74,
	.sysc_offs	= 0x78,
	.sysc_flags	= SYSC_HAS_SIDLEMODE,
	.idlemodes	= SIDLE_FORCE | SIDLE_NO |
			  SIDLE_SMART | SIDLE_SMART_WKUP,
	.sysc_fields	= &omap_hwmod_sysc_type3,
};

static struct omap_hwmod_class ti81xx_rtc_hwmod_class = {
	.name		= "rtc",
	.sysc		= &ti81xx_rtc_sysc,
};

static struct omap_hwmod ti81xx_rtc_hwmod = {
	.name		= "rtc",
	.class		= &ti81xx_rtc_hwmod_class,
	.clkdm_name	= "alwon_l3s_clkdm",
	.flags		= HWMOD_NO_IDLEST,
	.main_clk	= "sysclk18_ck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs = DM81XX_CM_ALWON_RTC_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_ocp_if ti81xx_l4_ls__rtc = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &ti81xx_rtc_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

/* UART common */
static struct omap_hwmod_class_sysconfig uart_sysc = {
	.rev_offs	= 0x50,
	.sysc_offs	= 0x54,
	.syss_offs	= 0x58,
	.sysc_flags	= SYSC_HAS_ENAWAKEUP | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
				SYSS_HAS_RESET_STATUS,
	.idlemodes	= SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
				MSTANDBY_SMART_WKUP,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class uart_class = {
	.name = "uart",
	.sysc = &uart_sysc,
};

static struct omap_hwmod dm81xx_uart1_hwmod = {
	.name		= "uart1",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk10_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_UART_0_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &uart_class,
	.flags		= DEBUG_TI81XXUART1_FLAGS,
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__uart1 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_uart1_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm81xx_uart2_hwmod = {
	.name		= "uart2",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk10_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_UART_1_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &uart_class,
	.flags		= DEBUG_TI81XXUART2_FLAGS,
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__uart2 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_uart2_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm81xx_uart3_hwmod = {
	.name		= "uart3",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk10_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_UART_2_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &uart_class,
	.flags		= DEBUG_TI81XXUART3_FLAGS,
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__uart3 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_uart3_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class_sysconfig wd_timer_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x10,
	.syss_offs	= 0x14,
	.sysc_flags	= SYSC_HAS_EMUFREE | SYSC_HAS_SOFTRESET |
				SYSS_HAS_RESET_STATUS,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class wd_timer_class = {
	.name		= "wd_timer",
	.sysc		= &wd_timer_sysc,
	.pre_shutdown	= &omap2_wd_timer_disable,
	.reset		= &omap2_wd_timer_reset,
};

static struct omap_hwmod dm81xx_wd_timer_hwmod = {
	.name		= "wd_timer",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk18_ck",
	.flags		= HWMOD_NO_IDLEST,
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_WDTIMER_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &wd_timer_class,
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__wd_timer1 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_wd_timer_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

/* I2C common */
static struct omap_hwmod_class_sysconfig i2c_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x10,
	.syss_offs	= 0x90,
	.sysc_flags	= SYSC_HAS_SIDLEMODE |
				SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
				SYSC_HAS_AUTOIDLE,
	.idlemodes	= SIDLE_FORCE | SIDLE_NO | SIDLE_SMART,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class i2c_class = {
	.name = "i2c",
	.sysc = &i2c_sysc,
};

static struct omap_hwmod dm81xx_i2c1_hwmod = {
	.name		= "i2c1",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk10_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_I2C_0_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &i2c_class,
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__i2c1 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_i2c1_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm81xx_i2c2_hwmod = {
	.name		= "i2c2",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk10_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_I2C_1_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &i2c_class,
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__i2c2 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_i2c2_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class_sysconfig dm81xx_elm_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_SOFTRESET |
				SYSS_HAS_RESET_STATUS,
	.idlemodes	= SIDLE_FORCE | SIDLE_NO | SIDLE_SMART,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class dm81xx_elm_hwmod_class = {
	.name = "elm",
	.sysc = &dm81xx_elm_sysc,
};

static struct omap_hwmod dm81xx_elm_hwmod = {
	.name		= "elm",
	.clkdm_name	= "alwon_l3s_clkdm",
	.class		= &dm81xx_elm_hwmod_class,
	.main_clk	= "sysclk6_ck",
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__elm = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_elm_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class_sysconfig dm81xx_gpio_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0114,
	.sysc_flags	= SYSC_HAS_AUTOIDLE | SYSC_HAS_ENAWAKEUP |
				SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET |
				SYSS_HAS_RESET_STATUS,
	.idlemodes	= SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
				SIDLE_SMART_WKUP,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class dm81xx_gpio_hwmod_class = {
	.name	= "gpio",
	.sysc	= &dm81xx_gpio_sysc,
	.rev	= 2,
};

static struct omap_hwmod_opt_clk gpio1_opt_clks[] = {
	{ .role = "dbclk", .clk = "sysclk18_ck" },
};

static struct omap_hwmod dm81xx_gpio1_hwmod = {
	.name		= "gpio1",
	.clkdm_name	= "alwon_l3s_clkdm",
	.class		= &dm81xx_gpio_hwmod_class,
	.main_clk	= "sysclk6_ck",
	.prcm = {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_GPIO_0_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.opt_clks	= gpio1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio1_opt_clks),
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__gpio1 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_gpio1_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_opt_clk gpio2_opt_clks[] = {
	{ .role = "dbclk", .clk = "sysclk18_ck" },
};

static struct omap_hwmod dm81xx_gpio2_hwmod = {
	.name		= "gpio2",
	.clkdm_name	= "alwon_l3s_clkdm",
	.class		= &dm81xx_gpio_hwmod_class,
	.main_clk	= "sysclk6_ck",
	.prcm = {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_GPIO_1_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.opt_clks	= gpio2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio2_opt_clks),
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__gpio2 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_gpio2_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_opt_clk gpio3_opt_clks[] = {
	{ .role = "dbclk", .clk = "sysclk18_ck" },
};

static struct omap_hwmod dm81xx_gpio3_hwmod = {
	.name		= "gpio3",
	.clkdm_name	= "alwon_l3s_clkdm",
	.class		= &dm81xx_gpio_hwmod_class,
	.main_clk	= "sysclk6_ck",
	.prcm = {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_GPIO_1_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.opt_clks	= gpio3_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio3_opt_clks),
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__gpio3 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_gpio3_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_opt_clk gpio4_opt_clks[] = {
	{ .role = "dbclk", .clk = "sysclk18_ck" },
};

static struct omap_hwmod dm81xx_gpio4_hwmod = {
	.name		= "gpio4",
	.clkdm_name	= "alwon_l3s_clkdm",
	.class		= &dm81xx_gpio_hwmod_class,
	.main_clk	= "sysclk6_ck",
	.prcm = {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_GPIO_1_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.opt_clks	= gpio4_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio4_opt_clks),
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__gpio4 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_gpio4_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class_sysconfig dm81xx_gpmc_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x10,
	.syss_offs	= 0x14,
	.sysc_flags	= SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET |
				SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS,
	.idlemodes	= SIDLE_FORCE | SIDLE_NO | SIDLE_SMART,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class dm81xx_gpmc_hwmod_class = {
	.name	= "gpmc",
	.sysc	= &dm81xx_gpmc_sysc,
};

static struct omap_hwmod dm81xx_gpmc_hwmod = {
	.name		= "gpmc",
	.clkdm_name	= "alwon_l3s_clkdm",
	.class		= &dm81xx_gpmc_hwmod_class,
	.main_clk	= "sysclk6_ck",
	/* Skip reset for CONFIG_OMAP_GPMC_DEBUG for bootloader timings */
	.flags		= DEBUG_OMAP_GPMC_HWMOD_FLAGS,
	.prcm = {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_GPMC_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_ocp_if dm81xx_alwon_l3_slow__gpmc = {
	.master		= &dm81xx_alwon_l3_slow_hwmod,
	.slave		= &dm81xx_gpmc_hwmod,
	.user		= OCP_USER_MPU,
};

/* USB needs udelay 1 after reset at least on hp t410, use 2 for margin */
static struct omap_hwmod_class_sysconfig dm81xx_usbhsotg_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x10,
	.srst_udelay	= 2,
	.sysc_flags	= SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE |
				SYSC_HAS_SOFTRESET,
	.idlemodes	= SIDLE_SMART | MSTANDBY_FORCE | MSTANDBY_SMART,
	.sysc_fields	= &omap_hwmod_sysc_type2,
};

static struct omap_hwmod_class dm81xx_usbotg_class = {
	.name = "usbotg",
	.sysc = &dm81xx_usbhsotg_sysc,
};

static struct omap_hwmod dm814x_usbss_hwmod = {
	.name		= "usb_otg_hs",
	.clkdm_name	= "default_l3_slow_clkdm",
	.main_clk	= "pll260dcoclkldo",	/* 481c5260.adpll.dcoclkldo */
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_DEFAULT_USB_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm81xx_usbotg_class,
};

static struct omap_hwmod_ocp_if dm814x_default_l3_slow__usbss = {
	.master		= &dm81xx_default_l3_slow_hwmod,
	.slave		= &dm814x_usbss_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm816x_usbss_hwmod = {
	.name		= "usb_otg_hs",
	.clkdm_name	= "default_l3_slow_clkdm",
	.main_clk	= "sysclk6_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_DEFAULT_USB_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm81xx_usbotg_class,
};

static struct omap_hwmod_ocp_if dm816x_default_l3_slow__usbss = {
	.master		= &dm81xx_default_l3_slow_hwmod,
	.slave		= &dm816x_usbss_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class_sysconfig dm816x_timer_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET,
	.idlemodes	= SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
				SIDLE_SMART_WKUP,
	.sysc_fields	= &omap_hwmod_sysc_type2,
};

static struct omap_hwmod_class dm816x_timer_hwmod_class = {
	.name = "timer",
	.sysc = &dm816x_timer_sysc,
};

static struct omap_hwmod dm814x_timer1_hwmod = {
	.name		= "timer1",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "timer1_fck",
	.class		= &dm816x_timer_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod_ocp_if dm814x_l4_ls__timer1 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm814x_timer1_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm816x_timer1_hwmod = {
	.name		= "timer1",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "timer1_fck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM816X_CM_ALWON_TIMER_1_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm816x_timer_hwmod_class,
};

static struct omap_hwmod_ocp_if dm816x_l4_ls__timer1 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm816x_timer1_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm814x_timer2_hwmod = {
	.name		= "timer2",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "timer2_fck",
	.class		= &dm816x_timer_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod_ocp_if dm814x_l4_ls__timer2 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm814x_timer2_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm816x_timer2_hwmod = {
	.name		= "timer2",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "timer2_fck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM816X_CM_ALWON_TIMER_2_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm816x_timer_hwmod_class,
};

static struct omap_hwmod_ocp_if dm816x_l4_ls__timer2 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm816x_timer2_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm816x_timer3_hwmod = {
	.name		= "timer3",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "timer3_fck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM816X_CM_ALWON_TIMER_3_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm816x_timer_hwmod_class,
};

static struct omap_hwmod_ocp_if dm816x_l4_ls__timer3 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm816x_timer3_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm816x_timer4_hwmod = {
	.name		= "timer4",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "timer4_fck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM816X_CM_ALWON_TIMER_4_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm816x_timer_hwmod_class,
};

static struct omap_hwmod_ocp_if dm816x_l4_ls__timer4 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm816x_timer4_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm816x_timer5_hwmod = {
	.name		= "timer5",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "timer5_fck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM816X_CM_ALWON_TIMER_5_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm816x_timer_hwmod_class,
};

static struct omap_hwmod_ocp_if dm816x_l4_ls__timer5 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm816x_timer5_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm816x_timer6_hwmod = {
	.name		= "timer6",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "timer6_fck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM816X_CM_ALWON_TIMER_6_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm816x_timer_hwmod_class,
};

static struct omap_hwmod_ocp_if dm816x_l4_ls__timer6 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm816x_timer6_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm816x_timer7_hwmod = {
	.name		= "timer7",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "timer7_fck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM816X_CM_ALWON_TIMER_7_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm816x_timer_hwmod_class,
};

static struct omap_hwmod_ocp_if dm816x_l4_ls__timer7 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm816x_timer7_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

/* CPSW on dm814x */
static struct omap_hwmod_class_sysconfig dm814x_cpgmac_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x8,
	.syss_offs	= 0x4,
	.sysc_flags	= SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE |
			  SYSS_HAS_RESET_STATUS,
	.idlemodes	= SIDLE_FORCE | SIDLE_NO | MSTANDBY_FORCE |
			  MSTANDBY_NO,
	.sysc_fields	= &omap_hwmod_sysc_type3,
};

static struct omap_hwmod_class dm814x_cpgmac0_hwmod_class = {
	.name		= "cpgmac0",
	.sysc		= &dm814x_cpgmac_sysc,
};

static struct omap_hwmod dm814x_cpgmac0_hwmod = {
	.name		= "cpgmac0",
	.class		= &dm814x_cpgmac0_hwmod_class,
	.clkdm_name	= "alwon_ethernet_clkdm",
	.flags		= HWMOD_SWSUP_SIDLE | HWMOD_SWSUP_MSTANDBY,
	.main_clk	= "cpsw_125mhz_gclk",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs = DM81XX_CM_ALWON_ETHERNET_0_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_class dm814x_mdio_hwmod_class = {
	.name		= "davinci_mdio",
};

static struct omap_hwmod dm814x_mdio_hwmod = {
	.name		= "davinci_mdio",
	.class		= &dm814x_mdio_hwmod_class,
	.clkdm_name	= "alwon_ethernet_clkdm",
	.main_clk	= "cpsw_125mhz_gclk",
};

static struct omap_hwmod_ocp_if dm814x_l4_hs__cpgmac0 = {
	.master		= &dm81xx_l4_hs_hwmod,
	.slave		= &dm814x_cpgmac0_hwmod,
	.clk		= "cpsw_125mhz_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if dm814x_cpgmac0__mdio = {
	.master		= &dm814x_cpgmac0_hwmod,
	.slave		= &dm814x_mdio_hwmod,
	.user		= OCP_USER_MPU,
	.flags		= HWMOD_NO_IDLEST,
};

/* EMAC Ethernet */
static struct omap_hwmod_class_sysconfig dm816x_emac_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x4,
	.sysc_flags	= SYSC_HAS_SOFTRESET,
	.sysc_fields	= &omap_hwmod_sysc_type2,
};

static struct omap_hwmod_class dm816x_emac_hwmod_class = {
	.name		= "emac",
	.sysc		= &dm816x_emac_sysc,
};

/*
 * On dm816x the MDIO is within EMAC0. As the MDIO driver is a separate
 * driver probed before EMAC0, we let MDIO do the clock idling.
 */
static struct omap_hwmod dm816x_emac0_hwmod = {
	.name		= "emac0",
	.clkdm_name	= "alwon_ethernet_clkdm",
	.class		= &dm816x_emac_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod_ocp_if dm81xx_l4_hs__emac0 = {
	.master		= &dm81xx_l4_hs_hwmod,
	.slave		= &dm816x_emac0_hwmod,
	.clk		= "sysclk5_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class dm81xx_mdio_hwmod_class = {
	.name		= "davinci_mdio",
	.sysc		= &dm816x_emac_sysc,
};

static struct omap_hwmod dm81xx_emac0_mdio_hwmod = {
	.name		= "davinci_mdio",
	.class		= &dm81xx_mdio_hwmod_class,
	.clkdm_name	= "alwon_ethernet_clkdm",
	.main_clk	= "sysclk24_ck",
	.flags		= HWMOD_NO_IDLEST,
	/*
	 * REVISIT: This should be moved to the emac0_hwmod
	 * once we have a better way to handle device slaves.
	 */
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_ETHERNET_0_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_ocp_if dm81xx_emac0__mdio = {
	.master		= &dm81xx_l4_hs_hwmod,
	.slave		= &dm81xx_emac0_mdio_hwmod,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm816x_emac1_hwmod = {
	.name		= "emac1",
	.clkdm_name	= "alwon_ethernet_clkdm",
	.main_clk	= "sysclk24_ck",
	.flags		= HWMOD_NO_IDLEST,
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM816X_CM_ALWON_ETHERNET_1_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm816x_emac_hwmod_class,
};

static struct omap_hwmod_ocp_if dm816x_l4_hs__emac1 = {
	.master		= &dm81xx_l4_hs_hwmod,
	.slave		= &dm816x_emac1_hwmod,
	.clk		= "sysclk5_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class_sysconfig dm81xx_sata_sysc = {
	.rev_offs	= 0x00fc,
	.sysc_offs	= 0x1100,
	.sysc_flags	= SYSC_HAS_SIDLEMODE,
	.idlemodes	= SIDLE_FORCE,
	.sysc_fields	= &omap_hwmod_sysc_type3,
};

static struct omap_hwmod_class dm81xx_sata_hwmod_class = {
	.name	= "sata",
	.sysc	= &dm81xx_sata_sysc,
};

static struct omap_hwmod dm81xx_sata_hwmod = {
	.name		= "sata",
	.clkdm_name	= "default_clkdm",
	.flags		= HWMOD_NO_IDLEST,
	.prcm = {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_DEFAULT_SATA_CLKCTRL,
			.modulemode   = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm81xx_sata_hwmod_class,
};

static struct omap_hwmod_ocp_if dm81xx_l4_hs__sata = {
	.master		= &dm81xx_l4_hs_hwmod,
	.slave		= &dm81xx_sata_hwmod,
	.clk		= "sysclk5_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class_sysconfig dm81xx_mmc_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x110,
	.syss_offs	= 0x114,
	.sysc_flags	= SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
				SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS,
	.idlemodes	= SIDLE_FORCE | SIDLE_NO | SIDLE_SMART,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class dm81xx_mmc_class = {
	.name = "mmc",
	.sysc = &dm81xx_mmc_sysc,
};

static struct omap_hwmod_opt_clk dm81xx_mmc_opt_clks[] = {
	{ .role = "dbck", .clk = "sysclk18_ck", },
};

static struct omap_hsmmc_dev_attr mmc_dev_attr = {
};

static struct omap_hwmod dm814x_mmc1_hwmod = {
	.name		= "mmc1",
	.clkdm_name	= "alwon_l3s_clkdm",
	.opt_clks	= dm81xx_mmc_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dm81xx_mmc_opt_clks),
	.main_clk	= "sysclk8_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM814X_CM_ALWON_MMCHS_0_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.dev_attr	= &mmc_dev_attr,
	.class		= &dm81xx_mmc_class,
};

static struct omap_hwmod_ocp_if dm814x_l4_ls__mmc1 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm814x_mmc1_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
	.flags		= OMAP_FIREWALL_L4
};

static struct omap_hwmod dm814x_mmc2_hwmod = {
	.name		= "mmc2",
	.clkdm_name	= "alwon_l3s_clkdm",
	.opt_clks	= dm81xx_mmc_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dm81xx_mmc_opt_clks),
	.main_clk	= "sysclk8_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM814X_CM_ALWON_MMCHS_1_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.dev_attr	= &mmc_dev_attr,
	.class		= &dm81xx_mmc_class,
};

static struct omap_hwmod_ocp_if dm814x_l4_ls__mmc2 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm814x_mmc2_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
	.flags		= OMAP_FIREWALL_L4
};

static struct omap_hwmod dm814x_mmc3_hwmod = {
	.name		= "mmc3",
	.clkdm_name	= "alwon_l3_med_clkdm",
	.opt_clks	= dm81xx_mmc_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dm81xx_mmc_opt_clks),
	.main_clk	= "sysclk8_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM814X_CM_ALWON_MMCHS_2_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.dev_attr	= &mmc_dev_attr,
	.class		= &dm81xx_mmc_class,
};

static struct omap_hwmod_ocp_if dm814x_alwon_l3_med__mmc3 = {
	.master		= &dm81xx_alwon_l3_med_hwmod,
	.slave		= &dm814x_mmc3_hwmod,
	.clk		= "sysclk4_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod dm816x_mmc1_hwmod = {
	.name		= "mmc1",
	.clkdm_name	= "alwon_l3s_clkdm",
	.opt_clks	= dm81xx_mmc_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dm81xx_mmc_opt_clks),
	.main_clk	= "sysclk10_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM816X_CM_ALWON_SDIO_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.dev_attr	= &mmc_dev_attr,
	.class		= &dm81xx_mmc_class,
};

static struct omap_hwmod_ocp_if dm816x_l4_ls__mmc1 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm816x_mmc1_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
	.flags		= OMAP_FIREWALL_L4
};

static struct omap_hwmod_class_sysconfig dm816x_mcspi_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x110,
	.syss_offs	= 0x114,
	.sysc_flags	= SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
				SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS,
	.idlemodes	= SIDLE_FORCE | SIDLE_NO | SIDLE_SMART,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class dm816x_mcspi_class = {
	.name = "mcspi",
	.sysc = &dm816x_mcspi_sysc,
};

static struct omap_hwmod dm81xx_mcspi1_hwmod = {
	.name		= "mcspi1",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk10_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_SPI_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm816x_mcspi_class,
};

static struct omap_hwmod dm81xx_mcspi2_hwmod = {
	.name		= "mcspi2",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk10_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_SPI_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm816x_mcspi_class,
};

static struct omap_hwmod dm81xx_mcspi3_hwmod = {
	.name		= "mcspi3",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk10_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_SPI_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm816x_mcspi_class,
};

static struct omap_hwmod dm81xx_mcspi4_hwmod = {
	.name		= "mcspi4",
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk10_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_SPI_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
	.class		= &dm816x_mcspi_class,
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__mcspi1 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_mcspi1_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__mcspi2 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_mcspi2_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__mcspi3 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_mcspi3_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__mcspi4 = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_mcspi4_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class_sysconfig dm81xx_mailbox_sysc = {
	.rev_offs	= 0x000,
	.sysc_offs	= 0x010,
	.syss_offs	= 0x014,
	.sysc_flags	= SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE,
	.idlemodes	= SIDLE_FORCE | SIDLE_NO | SIDLE_SMART,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class dm81xx_mailbox_hwmod_class = {
	.name = "mailbox",
	.sysc = &dm81xx_mailbox_sysc,
};

static struct omap_hwmod dm81xx_mailbox_hwmod = {
	.name		= "mailbox",
	.clkdm_name	= "alwon_l3s_clkdm",
	.class		= &dm81xx_mailbox_hwmod_class,
	.main_clk	= "sysclk6_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_MAILBOX_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__mailbox = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_mailbox_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class_sysconfig dm81xx_spinbox_sysc = {
	.rev_offs	= 0x000,
	.sysc_offs	= 0x010,
	.syss_offs	= 0x014,
	.sysc_flags	= SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE,
	.idlemodes	= SIDLE_FORCE | SIDLE_NO | SIDLE_SMART,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class dm81xx_spinbox_hwmod_class = {
	.name = "spinbox",
	.sysc = &dm81xx_spinbox_sysc,
};

static struct omap_hwmod dm81xx_spinbox_hwmod = {
	.name		= "spinbox",
	.clkdm_name	= "alwon_l3s_clkdm",
	.class		= &dm81xx_spinbox_hwmod_class,
	.main_clk	= "sysclk6_ck",
	.prcm		= {
		.omap4 = {
			.clkctrl_offs = DM81XX_CM_ALWON_SPINBOX_CLKCTRL,
			.modulemode = MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_ocp_if dm81xx_l4_ls__spinbox = {
	.master		= &dm81xx_l4_ls_hwmod,
	.slave		= &dm81xx_spinbox_hwmod,
	.clk		= "sysclk6_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class dm81xx_tpcc_hwmod_class = {
	.name		= "tpcc",
};

static struct omap_hwmod dm81xx_tpcc_hwmod = {
	.name		= "tpcc",
	.class		= &dm81xx_tpcc_hwmod_class,
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk4_ck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= DM81XX_CM_ALWON_TPCC_CLKCTRL,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_ocp_if dm81xx_alwon_l3_fast__tpcc = {
	.master		= &dm81xx_alwon_l3_fast_hwmod,
	.slave		= &dm81xx_tpcc_hwmod,
	.clk		= "sysclk4_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class dm81xx_tptc0_hwmod_class = {
	.name		= "tptc0",
};

static struct omap_hwmod dm81xx_tptc0_hwmod = {
	.name		= "tptc0",
	.class		= &dm81xx_tptc0_hwmod_class,
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk4_ck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= DM81XX_CM_ALWON_TPTC0_CLKCTRL,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_ocp_if dm81xx_alwon_l3_fast__tptc0 = {
	.master		= &dm81xx_alwon_l3_fast_hwmod,
	.slave		= &dm81xx_tptc0_hwmod,
	.clk		= "sysclk4_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if dm81xx_tptc0__alwon_l3_fast = {
	.master		= &dm81xx_tptc0_hwmod,
	.slave		= &dm81xx_alwon_l3_fast_hwmod,
	.clk		= "sysclk4_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class dm81xx_tptc1_hwmod_class = {
	.name		= "tptc1",
};

static struct omap_hwmod dm81xx_tptc1_hwmod = {
	.name		= "tptc1",
	.class		= &dm81xx_tptc1_hwmod_class,
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk4_ck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= DM81XX_CM_ALWON_TPTC1_CLKCTRL,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_ocp_if dm81xx_alwon_l3_fast__tptc1 = {
	.master		= &dm81xx_alwon_l3_fast_hwmod,
	.slave		= &dm81xx_tptc1_hwmod,
	.clk		= "sysclk4_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if dm81xx_tptc1__alwon_l3_fast = {
	.master		= &dm81xx_tptc1_hwmod,
	.slave		= &dm81xx_alwon_l3_fast_hwmod,
	.clk		= "sysclk4_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class dm81xx_tptc2_hwmod_class = {
	.name		= "tptc2",
};

static struct omap_hwmod dm81xx_tptc2_hwmod = {
	.name		= "tptc2",
	.class		= &dm81xx_tptc2_hwmod_class,
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk4_ck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= DM81XX_CM_ALWON_TPTC2_CLKCTRL,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_ocp_if dm81xx_alwon_l3_fast__tptc2 = {
	.master		= &dm81xx_alwon_l3_fast_hwmod,
	.slave		= &dm81xx_tptc2_hwmod,
	.clk		= "sysclk4_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if dm81xx_tptc2__alwon_l3_fast = {
	.master		= &dm81xx_tptc2_hwmod,
	.slave		= &dm81xx_alwon_l3_fast_hwmod,
	.clk		= "sysclk4_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class dm81xx_tptc3_hwmod_class = {
	.name		= "tptc3",
};

static struct omap_hwmod dm81xx_tptc3_hwmod = {
	.name		= "tptc3",
	.class		= &dm81xx_tptc3_hwmod_class,
	.clkdm_name	= "alwon_l3s_clkdm",
	.main_clk	= "sysclk4_ck",
	.prcm		= {
		.omap4	= {
			.clkctrl_offs	= DM81XX_CM_ALWON_TPTC3_CLKCTRL,
			.modulemode	= MODULEMODE_SWCTRL,
		},
	},
};

static struct omap_hwmod_ocp_if dm81xx_alwon_l3_fast__tptc3 = {
	.master		= &dm81xx_alwon_l3_fast_hwmod,
	.slave		= &dm81xx_tptc3_hwmod,
	.clk		= "sysclk4_ck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if dm81xx_tptc3__alwon_l3_fast = {
	.master		= &dm81xx_tptc3_hwmod,
	.slave		= &dm81xx_alwon_l3_fast_hwmod,
	.clk		= "sysclk4_ck",
	.user		= OCP_USER_MPU,
};

/*
 * REVISIT: Test and enable the following once clocks work:
 * dm81xx_l4_ls__mailbox
 *
 * Also note that some devices share a single clkctrl_offs..
 * For example, i2c1 and 3 share one, and i2c2 and 4 share one.
 */
static struct omap_hwmod_ocp_if *dm814x_hwmod_ocp_ifs[] __initdata = {
	&dm814x_mpu__alwon_l3_slow,
	&dm814x_mpu__alwon_l3_med,
	&dm81xx_alwon_l3_slow__l4_ls,
	&dm81xx_alwon_l3_slow__l4_hs,
	&dm81xx_l4_ls__uart1,
	&dm81xx_l4_ls__uart2,
	&dm81xx_l4_ls__uart3,
	&dm81xx_l4_ls__wd_timer1,
	&dm81xx_l4_ls__i2c1,
	&dm81xx_l4_ls__i2c2,
	&dm81xx_l4_ls__gpio1,
	&dm81xx_l4_ls__gpio2,
	&dm81xx_l4_ls__gpio3,
	&dm81xx_l4_ls__gpio4,
	&dm81xx_l4_ls__elm,
	&dm81xx_l4_ls__mcspi1,
	&dm81xx_l4_ls__mcspi2,
	&dm81xx_l4_ls__mcspi3,
	&dm81xx_l4_ls__mcspi4,
	&dm814x_l4_ls__mmc1,
	&dm814x_l4_ls__mmc2,
	&ti81xx_l4_ls__rtc,
	&dm81xx_alwon_l3_fast__tpcc,
	&dm81xx_alwon_l3_fast__tptc0,
	&dm81xx_alwon_l3_fast__tptc1,
	&dm81xx_alwon_l3_fast__tptc2,
	&dm81xx_alwon_l3_fast__tptc3,
	&dm81xx_tptc0__alwon_l3_fast,
	&dm81xx_tptc1__alwon_l3_fast,
	&dm81xx_tptc2__alwon_l3_fast,
	&dm81xx_tptc3__alwon_l3_fast,
	&dm814x_l4_ls__timer1,
	&dm814x_l4_ls__timer2,
	&dm814x_l4_hs__cpgmac0,
	&dm814x_cpgmac0__mdio,
	&dm81xx_alwon_l3_slow__gpmc,
	&dm814x_default_l3_slow__usbss,
	&dm814x_alwon_l3_med__mmc3,
	NULL,
};

int __init dm814x_hwmod_init(void)
{
	omap_hwmod_init();
	return omap_hwmod_register_links(dm814x_hwmod_ocp_ifs);
}

static struct omap_hwmod_ocp_if *dm816x_hwmod_ocp_ifs[] __initdata = {
	&dm816x_mpu__alwon_l3_slow,
	&dm816x_mpu__alwon_l3_med,
	&dm81xx_alwon_l3_slow__l4_ls,
	&dm81xx_alwon_l3_slow__l4_hs,
	&dm81xx_l4_ls__uart1,
	&dm81xx_l4_ls__uart2,
	&dm81xx_l4_ls__uart3,
	&dm81xx_l4_ls__wd_timer1,
	&dm81xx_l4_ls__i2c1,
	&dm81xx_l4_ls__i2c2,
	&dm81xx_l4_ls__gpio1,
	&dm81xx_l4_ls__gpio2,
	&dm81xx_l4_ls__elm,
	&ti81xx_l4_ls__rtc,
	&dm816x_l4_ls__mmc1,
	&dm816x_l4_ls__timer1,
	&dm816x_l4_ls__timer2,
	&dm816x_l4_ls__timer3,
	&dm816x_l4_ls__timer4,
	&dm816x_l4_ls__timer5,
	&dm816x_l4_ls__timer6,
	&dm816x_l4_ls__timer7,
	&dm81xx_l4_ls__mcspi1,
	&dm81xx_l4_ls__mailbox,
	&dm81xx_l4_ls__spinbox,
	&dm81xx_l4_hs__emac0,
	&dm81xx_emac0__mdio,
	&dm816x_l4_hs__emac1,
	&dm81xx_l4_hs__sata,
	&dm81xx_alwon_l3_fast__tpcc,
	&dm81xx_alwon_l3_fast__tptc0,
	&dm81xx_alwon_l3_fast__tptc1,
	&dm81xx_alwon_l3_fast__tptc2,
	&dm81xx_alwon_l3_fast__tptc3,
	&dm81xx_tptc0__alwon_l3_fast,
	&dm81xx_tptc1__alwon_l3_fast,
	&dm81xx_tptc2__alwon_l3_fast,
	&dm81xx_tptc3__alwon_l3_fast,
	&dm81xx_alwon_l3_slow__gpmc,
	&dm816x_default_l3_slow__usbss,
	NULL,
};

int __init dm816x_hwmod_init(void)
{
	omap_hwmod_init();
	return omap_hwmod_register_links(dm816x_hwmod_ocp_ifs);
}
