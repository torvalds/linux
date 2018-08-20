/*
 * omap_hwmod_3xxx_data.c - hardware modules present on the OMAP3xxx chips
 *
 * Copyright (C) 2009-2011 Nokia Corporation
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The data in this file should be completely autogeneratable from
 * the TI hardware database or other technical documentation.
 *
 * XXX these should be marked initdata for multi-OMAP kernels
 */

#include <linux/platform_data/i2c-omap.h>
#include <linux/power/smartreflex.h>
#include <linux/platform_data/hsmmc-omap.h>

#include <linux/omap-dma.h>
#include "l3_3xxx.h"
#include "l4_3xxx.h"

#include "soc.h"
#include "omap_hwmod.h"
#include "omap_hwmod_common_data.h"
#include "prm-regbits-34xx.h"
#include "cm-regbits-34xx.h"

#include "i2c.h"
#include "wd_timer.h"
#include "serial.h"

/*
 * OMAP3xxx hardware module integration data
 *
 * All of the data in this section should be autogeneratable from the
 * TI hardware database or other technical documentation.  Data that
 * is driver-specific or driver-kernel integration-specific belongs
 * elsewhere.
 */

#define AM35XX_IPSS_USBOTGSS_BASE      0x5C040000

/*
 * IP blocks
 */

/* L3 */

static struct omap_hwmod omap3xxx_l3_main_hwmod = {
	.name		= "l3_main",
	.class		= &l3_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

/* L4 CORE */
static struct omap_hwmod omap3xxx_l4_core_hwmod = {
	.name		= "l4_core",
	.class		= &l4_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

/* L4 PER */
static struct omap_hwmod omap3xxx_l4_per_hwmod = {
	.name		= "l4_per",
	.class		= &l4_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

/* L4 WKUP */
static struct omap_hwmod omap3xxx_l4_wkup_hwmod = {
	.name		= "l4_wkup",
	.class		= &l4_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

/* L4 SEC */
static struct omap_hwmod omap3xxx_l4_sec_hwmod = {
	.name		= "l4_sec",
	.class		= &l4_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

/* MPU */

static struct omap_hwmod omap3xxx_mpu_hwmod = {
	.name		= "mpu",
	.class		= &mpu_hwmod_class,
	.main_clk	= "arm_fck",
};

/* IVA2 (IVA2) */
static struct omap_hwmod_rst_info omap3xxx_iva_resets[] = {
	{ .name = "logic", .rst_shift = 0, .st_shift = 8 },
	{ .name = "seq0", .rst_shift = 1, .st_shift = 9 },
	{ .name = "seq1", .rst_shift = 2, .st_shift = 10 },
};

static struct omap_hwmod omap3xxx_iva_hwmod = {
	.name		= "iva",
	.class		= &iva_hwmod_class,
	.clkdm_name	= "iva2_clkdm",
	.rst_lines	= omap3xxx_iva_resets,
	.rst_lines_cnt	= ARRAY_SIZE(omap3xxx_iva_resets),
	.main_clk	= "iva2_ck",
	.prcm = {
		.omap2 = {
			.module_offs = OMAP3430_IVA2_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_IVA2_SHIFT,
		},
	},
};

/*
 * 'debugss' class
 * debug and emulation sub system
 */

static struct omap_hwmod_class omap3xxx_debugss_hwmod_class = {
	.name	= "debugss",
};

/* debugss */
static struct omap_hwmod omap3xxx_debugss_hwmod = {
	.name		= "debugss",
	.class		= &omap3xxx_debugss_hwmod_class,
	.clkdm_name	= "emu_clkdm",
	.main_clk	= "emu_src_ck",
	.flags		= HWMOD_NO_IDLEST,
};

/* timer class */
static struct omap_hwmod_class_sysconfig omap3xxx_timer_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_CLOCKACTIVITY |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_EMUFREE | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_timer_hwmod_class = {
	.name = "timer",
	.sysc = &omap3xxx_timer_sysc,
};

/* timer1 */
static struct omap_hwmod omap3xxx_timer1_hwmod = {
	.name		= "timer1",
	.main_clk	= "gpt1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT1_SHIFT,
		},
	},
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer2 */
static struct omap_hwmod omap3xxx_timer2_hwmod = {
	.name		= "timer2",
	.main_clk	= "gpt2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT2_SHIFT,
		},
	},
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer3 */
static struct omap_hwmod omap3xxx_timer3_hwmod = {
	.name		= "timer3",
	.main_clk	= "gpt3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT3_SHIFT,
		},
	},
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer4 */
static struct omap_hwmod omap3xxx_timer4_hwmod = {
	.name		= "timer4",
	.main_clk	= "gpt4_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT4_SHIFT,
		},
	},
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer5 */
static struct omap_hwmod omap3xxx_timer5_hwmod = {
	.name		= "timer5",
	.main_clk	= "gpt5_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT5_SHIFT,
		},
	},
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer6 */
static struct omap_hwmod omap3xxx_timer6_hwmod = {
	.name		= "timer6",
	.main_clk	= "gpt6_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT6_SHIFT,
		},
	},
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer7 */
static struct omap_hwmod omap3xxx_timer7_hwmod = {
	.name		= "timer7",
	.main_clk	= "gpt7_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT7_SHIFT,
		},
	},
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer8 */
static struct omap_hwmod omap3xxx_timer8_hwmod = {
	.name		= "timer8",
	.main_clk	= "gpt8_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT8_SHIFT,
		},
	},
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer9 */
static struct omap_hwmod omap3xxx_timer9_hwmod = {
	.name		= "timer9",
	.main_clk	= "gpt9_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT9_SHIFT,
		},
	},
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer10 */
static struct omap_hwmod omap3xxx_timer10_hwmod = {
	.name		= "timer10",
	.main_clk	= "gpt10_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT10_SHIFT,
		},
	},
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer11 */
static struct omap_hwmod omap3xxx_timer11_hwmod = {
	.name		= "timer11",
	.main_clk	= "gpt11_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT11_SHIFT,
		},
	},
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer12 */
static struct omap_hwmod omap3xxx_timer12_hwmod = {
	.name		= "timer12",
	.main_clk	= "gpt12_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT12_SHIFT,
		},
	},
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/*
 * 'wd_timer' class
 * 32-bit watchdog upward counter that generates a pulse on the reset pin on
 * overflow condition
 */

static struct omap_hwmod_class_sysconfig omap3xxx_wd_timer_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_EMUFREE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSC_HAS_CLOCKACTIVITY |
			   SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

/* I2C common */
static struct omap_hwmod_class_sysconfig i2c_sysc = {
	.rev_offs	= 0x00,
	.sysc_offs	= 0x20,
	.syss_offs	= 0x10,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_wd_timer_hwmod_class = {
	.name		= "wd_timer",
	.sysc		= &omap3xxx_wd_timer_sysc,
	.pre_shutdown	= &omap2_wd_timer_disable,
	.reset		= &omap2_wd_timer_reset,
};

static struct omap_hwmod omap3xxx_wd_timer2_hwmod = {
	.name		= "wd_timer2",
	.class		= &omap3xxx_wd_timer_hwmod_class,
	.main_clk	= "wdt2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_WDT2_SHIFT,
		},
	},
	/*
	 * XXX: Use software supervised mode, HW supervised smartidle seems to
	 * block CORE power domain idle transitions. Maybe a HW bug in wdt2?
	 */
	.flags		= HWMOD_SWSUP_SIDLE,
};

/* UART1 */
static struct omap_hwmod omap3xxx_uart1_hwmod = {
	.name		= "uart1",
	.main_clk	= "uart1_fck",
	.flags		= DEBUG_TI81XXUART1_FLAGS | HWMOD_SWSUP_SIDLE,
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_UART1_SHIFT,
		},
	},
	.class		= &omap2_uart_class,
};

/* UART2 */
static struct omap_hwmod omap3xxx_uart2_hwmod = {
	.name		= "uart2",
	.main_clk	= "uart2_fck",
	.flags		= DEBUG_TI81XXUART2_FLAGS | HWMOD_SWSUP_SIDLE,
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_UART2_SHIFT,
		},
	},
	.class		= &omap2_uart_class,
};

/* UART3 */
static struct omap_hwmod omap3xxx_uart3_hwmod = {
	.name		= "uart3",
	.main_clk	= "uart3_fck",
	.flags		= DEBUG_OMAP3UART3_FLAGS | DEBUG_TI81XXUART3_FLAGS |
				HWMOD_SWSUP_SIDLE,
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_UART3_SHIFT,
		},
	},
	.class		= &omap2_uart_class,
};

/* UART4 */


static struct omap_hwmod omap36xx_uart4_hwmod = {
	.name		= "uart4",
	.main_clk	= "uart4_fck",
	.flags		= DEBUG_OMAP3UART4_FLAGS | HWMOD_SWSUP_SIDLE,
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3630_EN_UART4_SHIFT,
		},
	},
	.class		= &omap2_uart_class,
};



/*
 * XXX AM35xx UART4 cannot complete its softreset without uart1_fck or
 * uart2_fck being enabled.  So we add uart1_fck as an optional clock,
 * below, and set the HWMOD_CONTROL_OPT_CLKS_IN_RESET.  This really
 * should not be needed.  The functional clock structure of the AM35xx
 * UART4 is extremely unclear and opaque; it is unclear what the role
 * of uart1/2_fck is for the UART4.  Any clarification from either
 * empirical testing or the AM3505/3517 hardware designers would be
 * most welcome.
 */
static struct omap_hwmod_opt_clk am35xx_uart4_opt_clks[] = {
	{ .role = "softreset_uart1_fck", .clk = "uart1_fck" },
};

static struct omap_hwmod am35xx_uart4_hwmod = {
	.name		= "uart4",
	.main_clk	= "uart4_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = AM35XX_ST_UART4_SHIFT,
		},
	},
	.opt_clks	= am35xx_uart4_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(am35xx_uart4_opt_clks),
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.class		= &omap2_uart_class,
};

static struct omap_hwmod_class i2c_class = {
	.name	= "i2c",
	.sysc	= &i2c_sysc,
	.rev	= OMAP_I2C_IP_VERSION_1,
	.reset	= &omap_i2c_reset,
};

/* dss */
static struct omap_hwmod_opt_clk dss_opt_clks[] = {
	/*
	 * The DSS HW needs all DSS clocks enabled during reset. The dss_core
	 * driver does not use these clocks.
	 */
	{ .role = "sys_clk", .clk = "dss2_alwon_fck" },
	{ .role = "tv_clk", .clk = "dss_tv_fck" },
	/* required only on OMAP3430 */
	{ .role = "tv_dac_clk", .clk = "dss_96m_fck" },
};

static struct omap_hwmod omap3430es1_dss_core_hwmod = {
	.name		= "dss_core",
	.class		= &omap2_dss_hwmod_class,
	.main_clk	= "dss1_alwon_fck", /* instead of dss_fck */
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_DSS_MOD,
			.idlest_reg_id = 1,
		},
	},
	.opt_clks	= dss_opt_clks,
	.opt_clks_cnt = ARRAY_SIZE(dss_opt_clks),
	.flags		= HWMOD_NO_IDLEST | HWMOD_CONTROL_OPT_CLKS_IN_RESET,
};

static struct omap_hwmod omap3xxx_dss_core_hwmod = {
	.name		= "dss_core",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.class		= &omap2_dss_hwmod_class,
	.main_clk	= "dss1_alwon_fck", /* instead of dss_fck */
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_DSS_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430ES2_ST_DSS_IDLE_SHIFT,
		},
	},
	.opt_clks	= dss_opt_clks,
	.opt_clks_cnt = ARRAY_SIZE(dss_opt_clks),
};

/*
 * 'dispc' class
 * display controller
 */

static struct omap_hwmod_class_sysconfig omap3_dispc_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSC_HAS_ENAWAKEUP),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			   MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3_dispc_hwmod_class = {
	.name	= "dispc",
	.sysc	= &omap3_dispc_sysc,
};

static struct omap_hwmod omap3xxx_dss_dispc_hwmod = {
	.name		= "dss_dispc",
	.class		= &omap3_dispc_hwmod_class,
	.main_clk	= "dss1_alwon_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_DSS_MOD,
		},
	},
	.flags		= HWMOD_NO_IDLEST,
	.dev_attr	= &omap2_3_dss_dispc_dev_attr,
};

/*
 * 'dsi' class
 * display serial interface controller
 */

static struct omap_hwmod_class_sysconfig omap3xxx_dsi_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_AUTOIDLE | SYSC_HAS_CLOCKACTIVITY |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_dsi_hwmod_class = {
	.name = "dsi",
	.sysc	= &omap3xxx_dsi_sysc,
};

/* dss_dsi1 */
static struct omap_hwmod_opt_clk dss_dsi1_opt_clks[] = {
	{ .role = "sys_clk", .clk = "dss2_alwon_fck" },
};

static struct omap_hwmod omap3xxx_dss_dsi1_hwmod = {
	.name		= "dss_dsi1",
	.class		= &omap3xxx_dsi_hwmod_class,
	.main_clk	= "dss1_alwon_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_DSS_MOD,
		},
	},
	.opt_clks	= dss_dsi1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dss_dsi1_opt_clks),
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod_opt_clk dss_rfbi_opt_clks[] = {
	{ .role = "ick", .clk = "dss_ick" },
};

static struct omap_hwmod omap3xxx_dss_rfbi_hwmod = {
	.name		= "dss_rfbi",
	.class		= &omap2_rfbi_hwmod_class,
	.main_clk	= "dss1_alwon_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_DSS_MOD,
		},
	},
	.opt_clks	= dss_rfbi_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dss_rfbi_opt_clks),
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod_opt_clk dss_venc_opt_clks[] = {
	/* required only on OMAP3430 */
	{ .role = "tv_dac_clk", .clk = "dss_96m_fck" },
};

static struct omap_hwmod omap3xxx_dss_venc_hwmod = {
	.name		= "dss_venc",
	.class		= &omap2_venc_hwmod_class,
	.main_clk	= "dss_tv_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_DSS_MOD,
		},
	},
	.opt_clks	= dss_venc_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dss_venc_opt_clks),
	.flags		= HWMOD_NO_IDLEST,
};

/* I2C1 */
static struct omap_hwmod omap3xxx_i2c1_hwmod = {
	.name		= "i2c1",
	.flags		= HWMOD_16BIT_REG | HWMOD_SET_DEFAULT_CLOCKACT,
	.main_clk	= "i2c1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_I2C1_SHIFT,
		},
	},
	.class		= &i2c_class,
};

/* I2C2 */
static struct omap_hwmod omap3xxx_i2c2_hwmod = {
	.name		= "i2c2",
	.flags		= HWMOD_16BIT_REG | HWMOD_SET_DEFAULT_CLOCKACT,
	.main_clk	= "i2c2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_I2C2_SHIFT,
		},
	},
	.class		= &i2c_class,
};

/* I2C3 */
static struct omap_hwmod omap3xxx_i2c3_hwmod = {
	.name		= "i2c3",
	.flags		= HWMOD_16BIT_REG | HWMOD_SET_DEFAULT_CLOCKACT,
	.main_clk	= "i2c3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_I2C3_SHIFT,
		},
	},
	.class		= &i2c_class,
};

/*
 * 'gpio' class
 * general purpose io module
 */

static struct omap_hwmod_class_sysconfig omap3xxx_gpio_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_ENAWAKEUP | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_gpio_hwmod_class = {
	.name = "gpio",
	.sysc = &omap3xxx_gpio_sysc,
	.rev = 1,
};

/* gpio1 */
static struct omap_hwmod_opt_clk gpio1_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio1_dbck", },
};

static struct omap_hwmod omap3xxx_gpio1_hwmod = {
	.name		= "gpio1",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.main_clk	= "gpio1_ick",
	.opt_clks	= gpio1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio1_opt_clks),
	.prcm		= {
		.omap2 = {
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO1_SHIFT,
		},
	},
	.class		= &omap3xxx_gpio_hwmod_class,
};

/* gpio2 */
static struct omap_hwmod_opt_clk gpio2_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio2_dbck", },
};

static struct omap_hwmod omap3xxx_gpio2_hwmod = {
	.name		= "gpio2",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.main_clk	= "gpio2_ick",
	.opt_clks	= gpio2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio2_opt_clks),
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO2_SHIFT,
		},
	},
	.class		= &omap3xxx_gpio_hwmod_class,
};

/* gpio3 */
static struct omap_hwmod_opt_clk gpio3_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio3_dbck", },
};

static struct omap_hwmod omap3xxx_gpio3_hwmod = {
	.name		= "gpio3",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.main_clk	= "gpio3_ick",
	.opt_clks	= gpio3_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio3_opt_clks),
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO3_SHIFT,
		},
	},
	.class		= &omap3xxx_gpio_hwmod_class,
};

/* gpio4 */
static struct omap_hwmod_opt_clk gpio4_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio4_dbck", },
};

static struct omap_hwmod omap3xxx_gpio4_hwmod = {
	.name		= "gpio4",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.main_clk	= "gpio4_ick",
	.opt_clks	= gpio4_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio4_opt_clks),
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO4_SHIFT,
		},
	},
	.class		= &omap3xxx_gpio_hwmod_class,
};

/* gpio5 */

static struct omap_hwmod_opt_clk gpio5_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio5_dbck", },
};

static struct omap_hwmod omap3xxx_gpio5_hwmod = {
	.name		= "gpio5",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.main_clk	= "gpio5_ick",
	.opt_clks	= gpio5_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio5_opt_clks),
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO5_SHIFT,
		},
	},
	.class		= &omap3xxx_gpio_hwmod_class,
};

/* gpio6 */

static struct omap_hwmod_opt_clk gpio6_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio6_dbck", },
};

static struct omap_hwmod omap3xxx_gpio6_hwmod = {
	.name		= "gpio6",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.main_clk	= "gpio6_ick",
	.opt_clks	= gpio6_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio6_opt_clks),
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO6_SHIFT,
		},
	},
	.class		= &omap3xxx_gpio_hwmod_class,
};

/* dma attributes */
static struct omap_dma_dev_attr dma_dev_attr = {
	.dev_caps  = RESERVE_CHANNEL | DMA_LINKED_LCH | GLOBAL_PRIORITY |
				IS_CSSA_32 | IS_CDSA_32 | IS_RW_PRIORITY,
	.lch_count = 32,
};

static struct omap_hwmod_class_sysconfig omap3xxx_dma_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x002c,
	.syss_offs	= 0x0028,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_MIDLEMODE | SYSC_HAS_CLOCKACTIVITY |
			   SYSC_HAS_EMUFREE | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			   MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_dma_hwmod_class = {
	.name = "dma",
	.sysc = &omap3xxx_dma_sysc,
};

/* dma_system */
static struct omap_hwmod omap3xxx_dma_system_hwmod = {
	.name		= "dma",
	.class		= &omap3xxx_dma_hwmod_class,
	.main_clk	= "core_l3_ick",
	.prcm = {
		.omap2 = {
			.module_offs		= CORE_MOD,
			.idlest_reg_id		= 1,
			.idlest_idle_bit	= OMAP3430_ST_SDMA_SHIFT,
		},
	},
	.dev_attr	= &dma_dev_attr,
	.flags		= HWMOD_NO_IDLEST,
};

/*
 * 'mcbsp' class
 * multi channel buffered serial port controller
 */

static struct omap_hwmod_class_sysconfig omap3xxx_mcbsp_sysc = {
	.rev_offs	= -ENODEV,
	.sysc_offs	= 0x008c,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_ENAWAKEUP |
			   SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_mcbsp_hwmod_class = {
	.name = "mcbsp",
	.sysc = &omap3xxx_mcbsp_sysc,
};

/* McBSP functional clock mapping */
static struct omap_hwmod_opt_clk mcbsp15_opt_clks[] = {
	{ .role = "pad_fck", .clk = "mcbsp_clks" },
	{ .role = "prcm_fck", .clk = "core_96m_fck" },
};

static struct omap_hwmod_opt_clk mcbsp234_opt_clks[] = {
	{ .role = "pad_fck", .clk = "mcbsp_clks" },
	{ .role = "prcm_fck", .clk = "per_96m_fck" },
};

/* mcbsp1 */
static struct omap_hwmod omap3xxx_mcbsp1_hwmod = {
	.name		= "mcbsp1",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.main_clk	= "mcbsp1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP1_SHIFT,
		},
	},
	.opt_clks	= mcbsp15_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp15_opt_clks),
};

/* mcbsp2 */
static struct omap_hwmod omap3xxx_mcbsp2_hwmod = {
	.name		= "mcbsp2",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.main_clk	= "mcbsp2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP2_SHIFT,
		},
	},
	.opt_clks	= mcbsp234_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp234_opt_clks),
};

/* mcbsp3 */
static struct omap_hwmod omap3xxx_mcbsp3_hwmod = {
	.name		= "mcbsp3",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.main_clk	= "mcbsp3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP3_SHIFT,
		},
	},
	.opt_clks	= mcbsp234_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp234_opt_clks),
};

/* mcbsp4 */
static struct omap_hwmod omap3xxx_mcbsp4_hwmod = {
	.name		= "mcbsp4",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.main_clk	= "mcbsp4_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP4_SHIFT,
		},
	},
	.opt_clks	= mcbsp234_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp234_opt_clks),
};

/* mcbsp5 */
static struct omap_hwmod omap3xxx_mcbsp5_hwmod = {
	.name		= "mcbsp5",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.main_clk	= "mcbsp5_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP5_SHIFT,
		},
	},
	.opt_clks	= mcbsp15_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp15_opt_clks),
};

/* 'mcbsp sidetone' class */
static struct omap_hwmod_class_sysconfig omap3xxx_mcbsp_sidetone_sysc = {
	.rev_offs	= -ENODEV,
	.sysc_offs	= 0x0010,
	.sysc_flags	= SYSC_HAS_AUTOIDLE,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_mcbsp_sidetone_hwmod_class = {
	.name = "mcbsp_sidetone",
	.sysc = &omap3xxx_mcbsp_sidetone_sysc,
};

/* mcbsp2_sidetone */
static struct omap_hwmod omap3xxx_mcbsp2_sidetone_hwmod = {
	.name		= "mcbsp2_sidetone",
	.class		= &omap3xxx_mcbsp_sidetone_hwmod_class,
	.main_clk	= "mcbsp2_ick",
	.flags		= HWMOD_NO_IDLEST,
};

/* mcbsp3_sidetone */
static struct omap_hwmod omap3xxx_mcbsp3_sidetone_hwmod = {
	.name		= "mcbsp3_sidetone",
	.class		= &omap3xxx_mcbsp_sidetone_hwmod_class,
	.main_clk	= "mcbsp3_ick",
	.flags		= HWMOD_NO_IDLEST,
};

/* SR common */
static struct omap_hwmod_class_sysconfig omap34xx_sr_sysc = {
	.rev_offs	= -ENODEV,
	.sysc_offs	= 0x24,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_NO_CACHE),
	.sysc_fields	= &omap34xx_sr_sysc_fields,
};

static struct omap_hwmod_class omap34xx_smartreflex_hwmod_class = {
	.name = "smartreflex",
	.sysc = &omap34xx_sr_sysc,
	.rev  = 1,
};

static struct omap_hwmod_class_sysconfig omap36xx_sr_sysc = {
	.rev_offs	= -ENODEV,
	.sysc_offs	= 0x38,
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_ENAWAKEUP |
			SYSC_NO_CACHE),
	.sysc_fields	= &omap36xx_sr_sysc_fields,
};

static struct omap_hwmod_class omap36xx_smartreflex_hwmod_class = {
	.name = "smartreflex",
	.sysc = &omap36xx_sr_sysc,
	.rev  = 2,
};

/* SR1 */
static struct omap_smartreflex_dev_attr sr1_dev_attr = {
	.sensor_voltdm_name   = "mpu_iva",
};


static struct omap_hwmod omap34xx_sr1_hwmod = {
	.name		= "smartreflex_mpu_iva",
	.class		= &omap34xx_smartreflex_hwmod_class,
	.main_clk	= "sr1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_SR1_SHIFT,
		},
	},
	.dev_attr	= &sr1_dev_attr,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

static struct omap_hwmod omap36xx_sr1_hwmod = {
	.name		= "smartreflex_mpu_iva",
	.class		= &omap36xx_smartreflex_hwmod_class,
	.main_clk	= "sr1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_SR1_SHIFT,
		},
	},
	.dev_attr	= &sr1_dev_attr,
};

/* SR2 */
static struct omap_smartreflex_dev_attr sr2_dev_attr = {
	.sensor_voltdm_name	= "core",
};


static struct omap_hwmod omap34xx_sr2_hwmod = {
	.name		= "smartreflex_core",
	.class		= &omap34xx_smartreflex_hwmod_class,
	.main_clk	= "sr2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_SR2_SHIFT,
		},
	},
	.dev_attr	= &sr2_dev_attr,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

static struct omap_hwmod omap36xx_sr2_hwmod = {
	.name		= "smartreflex_core",
	.class		= &omap36xx_smartreflex_hwmod_class,
	.main_clk	= "sr2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_SR2_SHIFT,
		},
	},
	.dev_attr	= &sr2_dev_attr,
};

/*
 * 'mailbox' class
 * mailbox module allowing communication between the on-chip processors
 * using a queued mailbox-interrupt mechanism.
 */

static struct omap_hwmod_class_sysconfig omap3xxx_mailbox_sysc = {
	.rev_offs	= 0x000,
	.sysc_offs	= 0x010,
	.syss_offs	= 0x014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_mailbox_hwmod_class = {
	.name = "mailbox",
	.sysc = &omap3xxx_mailbox_sysc,
};

static struct omap_hwmod omap3xxx_mailbox_hwmod = {
	.name		= "mailbox",
	.class		= &omap3xxx_mailbox_hwmod_class,
	.main_clk	= "mailboxes_ick",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MAILBOXES_SHIFT,
		},
	},
};

/*
 * 'mcspi' class
 * multichannel serial port interface (mcspi) / master/slave synchronous serial
 * bus
 */

static struct omap_hwmod_class_sysconfig omap34xx_mcspi_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
				SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap34xx_mcspi_class = {
	.name = "mcspi",
	.sysc = &omap34xx_mcspi_sysc,
};

/* mcspi1 */
static struct omap_hwmod omap34xx_mcspi1 = {
	.name		= "mcspi1",
	.main_clk	= "mcspi1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCSPI1_SHIFT,
		},
	},
	.class		= &omap34xx_mcspi_class,
};

/* mcspi2 */
static struct omap_hwmod omap34xx_mcspi2 = {
	.name		= "mcspi2",
	.main_clk	= "mcspi2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCSPI2_SHIFT,
		},
	},
	.class		= &omap34xx_mcspi_class,
};

/* mcspi3 */
static struct omap_hwmod omap34xx_mcspi3 = {
	.name		= "mcspi3",
	.main_clk	= "mcspi3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCSPI3_SHIFT,
		},
	},
	.class		= &omap34xx_mcspi_class,
};

/* mcspi4 */
static struct omap_hwmod omap34xx_mcspi4 = {
	.name		= "mcspi4",
	.main_clk	= "mcspi4_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCSPI4_SHIFT,
		},
	},
	.class		= &omap34xx_mcspi_class,
};

/* usbhsotg */
static struct omap_hwmod_class_sysconfig omap3xxx_usbhsotg_sysc = {
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
	.sysc = &omap3xxx_usbhsotg_sysc,
};

/* usb_otg_hs */

static struct omap_hwmod omap3xxx_usbhsotg_hwmod = {
	.name		= "usb_otg_hs",
	.main_clk	= "hsotgusb_ick",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430ES2_ST_HSOTGUSB_IDLE_SHIFT,
		},
	},
	.class		= &usbotg_class,

	/*
	 * Erratum ID: i479  idle_req / idle_ack mechanism potentially
	 * broken when autoidle is enabled
	 * workaround is to disable the autoidle bit at module level.
	 *
	 * Enabling the device in any other MIDLEMODE setting but force-idle
	 * causes core_pwrdm not enter idle states at least on OMAP3630.
	 * Note that musb has OTG_FORCESTDBY register that controls MSTANDBY
	 * signal when MIDLEMODE is set to force-idle.
	 */
	.flags		= HWMOD_NO_OCP_AUTOIDLE | HWMOD_SWSUP_SIDLE |
			  HWMOD_FORCE_MSTANDBY | HWMOD_RECONFIG_IO_CHAIN,
};

/* usb_otg_hs */

static struct omap_hwmod_class am35xx_usbotg_class = {
	.name = "am35xx_usbotg",
};

static struct omap_hwmod am35xx_usbhsotg_hwmod = {
	.name		= "am35x_otg_hs",
	.main_clk	= "hsotgusb_fck",
	.class		= &am35xx_usbotg_class,
	.flags		= HWMOD_NO_IDLEST,
};

/* MMC/SD/SDIO common */
static struct omap_hwmod_class_sysconfig omap34xx_mmc_sysc = {
	.rev_offs	= 0x1fc,
	.sysc_offs	= 0x10,
	.syss_offs	= 0x14,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap34xx_mmc_class = {
	.name = "mmc",
	.sysc = &omap34xx_mmc_sysc,
};

/* MMC/SD/SDIO1 */



static struct omap_hwmod_opt_clk omap34xx_mmc1_opt_clks[] = {
	{ .role = "dbck", .clk = "omap_32k_fck", },
};

static struct omap_hsmmc_dev_attr mmc1_dev_attr = {
	.flags = OMAP_HSMMC_SUPPORTS_DUAL_VOLT,
};

/* See 35xx errata 2.1.1.128 in SPRZ278F */
static struct omap_hsmmc_dev_attr mmc1_pre_es3_dev_attr = {
	.flags = (OMAP_HSMMC_SUPPORTS_DUAL_VOLT |
		  OMAP_HSMMC_BROKEN_MULTIBLOCK_READ),
};

static struct omap_hwmod omap3xxx_pre_es3_mmc1_hwmod = {
	.name		= "mmc1",
	.opt_clks	= omap34xx_mmc1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc1_opt_clks),
	.main_clk	= "mmchs1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC1_SHIFT,
		},
	},
	.dev_attr	= &mmc1_pre_es3_dev_attr,
	.class		= &omap34xx_mmc_class,
};

static struct omap_hwmod omap3xxx_es3plus_mmc1_hwmod = {
	.name		= "mmc1",
	.opt_clks	= omap34xx_mmc1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc1_opt_clks),
	.main_clk	= "mmchs1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC1_SHIFT,
		},
	},
	.dev_attr	= &mmc1_dev_attr,
	.class		= &omap34xx_mmc_class,
};

/* MMC/SD/SDIO2 */



static struct omap_hwmod_opt_clk omap34xx_mmc2_opt_clks[] = {
	{ .role = "dbck", .clk = "omap_32k_fck", },
};

/* See 35xx errata 2.1.1.128 in SPRZ278F */
static struct omap_hsmmc_dev_attr mmc2_pre_es3_dev_attr = {
	.flags = OMAP_HSMMC_BROKEN_MULTIBLOCK_READ,
};

static struct omap_hwmod omap3xxx_pre_es3_mmc2_hwmod = {
	.name		= "mmc2",
	.opt_clks	= omap34xx_mmc2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc2_opt_clks),
	.main_clk	= "mmchs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC2_SHIFT,
		},
	},
	.dev_attr	= &mmc2_pre_es3_dev_attr,
	.class		= &omap34xx_mmc_class,
};

static struct omap_hwmod omap3xxx_es3plus_mmc2_hwmod = {
	.name		= "mmc2",
	.opt_clks	= omap34xx_mmc2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc2_opt_clks),
	.main_clk	= "mmchs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC2_SHIFT,
		},
	},
	.class		= &omap34xx_mmc_class,
};

/* MMC/SD/SDIO3 */



static struct omap_hwmod_opt_clk omap34xx_mmc3_opt_clks[] = {
	{ .role = "dbck", .clk = "omap_32k_fck", },
};

static struct omap_hwmod omap3xxx_mmc3_hwmod = {
	.name		= "mmc3",
	.opt_clks	= omap34xx_mmc3_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc3_opt_clks),
	.main_clk	= "mmchs3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC3_SHIFT,
		},
	},
	.class		= &omap34xx_mmc_class,
};

/*
 * 'usb_host_hs' class
 * high-speed multi-port usb host controller
 */

static struct omap_hwmod_class_sysconfig omap3xxx_usb_host_hs_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_MIDLEMODE | SYSC_HAS_CLOCKACTIVITY |
			   SYSC_HAS_SIDLEMODE | SYSC_HAS_ENAWAKEUP |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			   MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_usb_host_hs_hwmod_class = {
	.name = "usb_host_hs",
	.sysc = &omap3xxx_usb_host_hs_sysc,
};


static struct omap_hwmod omap3xxx_usb_host_hs_hwmod = {
	.name		= "usb_host_hs",
	.class		= &omap3xxx_usb_host_hs_hwmod_class,
	.clkdm_name	= "usbhost_clkdm",
	.main_clk	= "usbhost_48m_fck",
	.prcm = {
		.omap2 = {
			.module_offs = OMAP3430ES2_USBHOST_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430ES2_ST_USBHOST_IDLE_SHIFT,
		},
	},

	/*
	 * Errata: USBHOST Configured In Smart-Idle Can Lead To a Deadlock
	 * id: i660
	 *
	 * Description:
	 * In the following configuration :
	 * - USBHOST module is set to smart-idle mode
	 * - PRCM asserts idle_req to the USBHOST module ( This typically
	 *   happens when the system is going to a low power mode : all ports
	 *   have been suspended, the master part of the USBHOST module has
	 *   entered the standby state, and SW has cut the functional clocks)
	 * - an USBHOST interrupt occurs before the module is able to answer
	 *   idle_ack, typically a remote wakeup IRQ.
	 * Then the USB HOST module will enter a deadlock situation where it
	 * is no more accessible nor functional.
	 *
	 * Workaround:
	 * Don't use smart idle; use only force idle, hence HWMOD_SWSUP_SIDLE
	 */

	/*
	 * Errata: USB host EHCI may stall when entering smart-standby mode
	 * Id: i571
	 *
	 * Description:
	 * When the USBHOST module is set to smart-standby mode, and when it is
	 * ready to enter the standby state (i.e. all ports are suspended and
	 * all attached devices are in suspend mode), then it can wrongly assert
	 * the Mstandby signal too early while there are still some residual OCP
	 * transactions ongoing. If this condition occurs, the internal state
	 * machine may go to an undefined state and the USB link may be stuck
	 * upon the next resume.
	 *
	 * Workaround:
	 * Don't use smart standby; use only force standby,
	 * hence HWMOD_SWSUP_MSTANDBY
	 */

	.flags		= HWMOD_SWSUP_SIDLE | HWMOD_SWSUP_MSTANDBY,
};

/*
 * 'usb_tll_hs' class
 * usb_tll_hs module is the adapter on the usb_host_hs ports
 */
static struct omap_hwmod_class_sysconfig omap3xxx_usb_tll_hs_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_usb_tll_hs_hwmod_class = {
	.name = "usb_tll_hs",
	.sysc = &omap3xxx_usb_tll_hs_sysc,
};


static struct omap_hwmod omap3xxx_usb_tll_hs_hwmod = {
	.name		= "usb_tll_hs",
	.class		= &omap3xxx_usb_tll_hs_hwmod_class,
	.clkdm_name	= "core_l4_clkdm",
	.main_clk	= "usbtll_fck",
	.prcm = {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 3,
			.idlest_idle_bit = OMAP3430ES2_ST_USBTLL_SHIFT,
		},
	},
};

static struct omap_hwmod omap3xxx_hdq1w_hwmod = {
	.name		= "hdq1w",
	.main_clk	= "hdq_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_HDQ_SHIFT,
		},
	},
	.class		= &omap2_hdq1w_class,
};

/* SAD2D */
static struct omap_hwmod_rst_info omap3xxx_sad2d_resets[] = {
	{ .name = "rst_modem_pwron_sw", .rst_shift = 0 },
	{ .name = "rst_modem_sw", .rst_shift = 1 },
};

static struct omap_hwmod_class omap3xxx_sad2d_class = {
	.name			= "sad2d",
};

static struct omap_hwmod omap3xxx_sad2d_hwmod = {
	.name		= "sad2d",
	.rst_lines	= omap3xxx_sad2d_resets,
	.rst_lines_cnt	= ARRAY_SIZE(omap3xxx_sad2d_resets),
	.main_clk	= "sad2d_ick",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_SAD2D_SHIFT,
		},
	},
	.class		= &omap3xxx_sad2d_class,
};

/*
 * '32K sync counter' class
 * 32-bit ordinary counter, clocked by the falling edge of the 32 khz clock
 */
static struct omap_hwmod_class_sysconfig omap3xxx_counter_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0004,
	.sysc_flags	= SYSC_HAS_SIDLEMODE,
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_counter_hwmod_class = {
	.name	= "counter",
	.sysc	= &omap3xxx_counter_sysc,
};

static struct omap_hwmod omap3xxx_counter_32k_hwmod = {
	.name		= "counter_32k",
	.class		= &omap3xxx_counter_hwmod_class,
	.clkdm_name	= "wkup_clkdm",
	.flags		= HWMOD_SWSUP_SIDLE,
	.main_clk	= "wkup_32k_fck",
	.prcm		= {
		.omap2	= {
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_32KSYNC_SHIFT,
		},
	},
};

/*
 * 'gpmc' class
 * general purpose memory controller
 */

static struct omap_hwmod_class_sysconfig omap3xxx_gpmc_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_AUTOIDLE | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_gpmc_hwmod_class = {
	.name	= "gpmc",
	.sysc	= &omap3xxx_gpmc_sysc,
};

static struct omap_hwmod omap3xxx_gpmc_hwmod = {
	.name		= "gpmc",
	.class		= &omap3xxx_gpmc_hwmod_class,
	.clkdm_name	= "core_l3_clkdm",
	.main_clk	= "gpmc_fck",
	/* Skip reset for CONFIG_OMAP_GPMC_DEBUG for bootloader timings */
	.flags		= HWMOD_NO_IDLEST | DEBUG_OMAP_GPMC_HWMOD_FLAGS,
};

/*
 * interfaces
 */

/* L3 -> L4_CORE interface */
static struct omap_hwmod_ocp_if omap3xxx_l3_main__l4_core = {
	.master	= &omap3xxx_l3_main_hwmod,
	.slave	= &omap3xxx_l4_core_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L3 -> L4_PER interface */
static struct omap_hwmod_ocp_if omap3xxx_l3_main__l4_per = {
	.master = &omap3xxx_l3_main_hwmod,
	.slave	= &omap3xxx_l4_per_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};


/* MPU -> L3 interface */
static struct omap_hwmod_ocp_if omap3xxx_mpu__l3_main = {
	.master   = &omap3xxx_mpu_hwmod,
	.slave    = &omap3xxx_l3_main_hwmod,
	.user	= OCP_USER_MPU,
};


/* l3 -> debugss */
static struct omap_hwmod_ocp_if omap3xxx_l3_main__l4_debugss = {
	.master		= &omap3xxx_l3_main_hwmod,
	.slave		= &omap3xxx_debugss_hwmod,
	.user		= OCP_USER_MPU,
};

/* DSS -> l3 */
static struct omap_hwmod_ocp_if omap3430es1_dss__l3 = {
	.master		= &omap3430es1_dss_core_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if omap3xxx_dss__l3 = {
	.master		= &omap3xxx_dss_core_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.fw = {
		.omap2 = {
			.l3_perm_bit  = OMAP3_L3_CORE_FW_INIT_ID_DSS,
			.flags	= OMAP_FIREWALL_L3,
		},
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l3_core -> usbhsotg interface */
static struct omap_hwmod_ocp_if omap3xxx_usbhsotg__l3 = {
	.master		= &omap3xxx_usbhsotg_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "core_l3_ick",
	.user		= OCP_USER_MPU,
};

/* l3_core -> am35xx_usbhsotg interface */
static struct omap_hwmod_ocp_if am35xx_usbhsotg__l3 = {
	.master		= &am35xx_usbhsotg_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "hsotgusb_ick",
	.user		= OCP_USER_MPU,
};

/* l3_core -> sad2d interface */
static struct omap_hwmod_ocp_if omap3xxx_sad2d__l3 = {
	.master		= &omap3xxx_sad2d_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "core_l3_ick",
	.user		= OCP_USER_MPU,
};

/* L4_CORE -> L4_WKUP interface */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__l4_wkup = {
	.master	= &omap3xxx_l4_core_hwmod,
	.slave	= &omap3xxx_l4_wkup_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> MMC1 interface */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__pre_es3_mmc1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_pre_es3_mmc1_hwmod,
	.clk		= "mmchs1_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4,
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__es3plus_mmc1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_es3plus_mmc1_hwmod,
	.clk		= "mmchs1_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4,
};

/* L4 CORE -> MMC2 interface */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__pre_es3_mmc2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_pre_es3_mmc2_hwmod,
	.clk		= "mmchs2_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4,
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__es3plus_mmc2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_es3plus_mmc2_hwmod,
	.clk		= "mmchs2_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4,
};

/* L4 CORE -> MMC3 interface */

static struct omap_hwmod_ocp_if omap3xxx_l4_core__mmc3 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mmc3_hwmod,
	.clk		= "mmchs3_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4,
};

/* L4 CORE -> UART1 interface */

static struct omap_hwmod_ocp_if omap3_l4_core__uart1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_uart1_hwmod,
	.clk		= "uart1_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> UART2 interface */

static struct omap_hwmod_ocp_if omap3_l4_core__uart2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_uart2_hwmod,
	.clk		= "uart2_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 PER -> UART3 interface */

static struct omap_hwmod_ocp_if omap3_l4_per__uart3 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_uart3_hwmod,
	.clk		= "uart3_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 PER -> UART4 interface */

static struct omap_hwmod_ocp_if omap36xx_l4_per__uart4 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap36xx_uart4_hwmod,
	.clk		= "uart4_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* AM35xx: L4 CORE -> UART4 interface */

static struct omap_hwmod_ocp_if am35xx_l4_core__uart4 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &am35xx_uart4_hwmod,
	.clk		= "uart4_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C1 interface */
static struct omap_hwmod_ocp_if omap3_l4_core__i2c1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_i2c1_hwmod,
	.clk		= "i2c1_ick",
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_I2C1_REGION,
			.l4_prot_group = 7,
			.flags	= OMAP_FIREWALL_L4,
		},
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C2 interface */
static struct omap_hwmod_ocp_if omap3_l4_core__i2c2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_i2c2_hwmod,
	.clk		= "i2c2_ick",
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_I2C2_REGION,
			.l4_prot_group = 7,
			.flags = OMAP_FIREWALL_L4,
		},
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C3 interface */

static struct omap_hwmod_ocp_if omap3_l4_core__i2c3 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_i2c3_hwmod,
	.clk		= "i2c3_ick",
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_I2C3_REGION,
			.l4_prot_group = 7,
			.flags = OMAP_FIREWALL_L4,
		},
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> SR1 interface */
static struct omap_hwmod_ocp_if omap34xx_l4_core__sr1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_sr1_hwmod,
	.clk		= "sr_l4_ick",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if omap36xx_l4_core__sr1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap36xx_sr1_hwmod,
	.clk		= "sr_l4_ick",
	.user		= OCP_USER_MPU,
};

/* L4 CORE -> SR2 interface */

static struct omap_hwmod_ocp_if omap34xx_l4_core__sr2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_sr2_hwmod,
	.clk		= "sr_l4_ick",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if omap36xx_l4_core__sr2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap36xx_sr2_hwmod,
	.clk		= "sr_l4_ick",
	.user		= OCP_USER_MPU,
};


/* l4_core -> usbhsotg  */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__usbhsotg = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_usbhsotg_hwmod,
	.clk		= "l4_ick",
	.user		= OCP_USER_MPU,
};


/* l4_core -> usbhsotg  */
static struct omap_hwmod_ocp_if am35xx_l4_core__usbhsotg = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &am35xx_usbhsotg_hwmod,
	.clk		= "hsotgusb_ick",
	.user		= OCP_USER_MPU,
};

/* L4_WKUP -> L4_SEC interface */
static struct omap_hwmod_ocp_if omap3xxx_l4_wkup__l4_sec = {
	.master = &omap3xxx_l4_wkup_hwmod,
	.slave	= &omap3xxx_l4_sec_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* IVA2 <- L3 interface */
static struct omap_hwmod_ocp_if omap3xxx_l3__iva = {
	.master		= &omap3xxx_l3_main_hwmod,
	.slave		= &omap3xxx_iva_hwmod,
	.clk		= "core_l3_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_wkup -> timer1 */
static struct omap_hwmod_ocp_if omap3xxx_l4_wkup__timer1 = {
	.master		= &omap3xxx_l4_wkup_hwmod,
	.slave		= &omap3xxx_timer1_hwmod,
	.clk		= "gpt1_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_per -> timer2 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer2 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer2_hwmod,
	.clk		= "gpt2_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_per -> timer3 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer3 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer3_hwmod,
	.clk		= "gpt3_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_per -> timer4 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer4 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer4_hwmod,
	.clk		= "gpt4_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_per -> timer5 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer5 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer5_hwmod,
	.clk		= "gpt5_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_per -> timer6 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer6 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer6_hwmod,
	.clk		= "gpt6_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_per -> timer7 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer7 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer7_hwmod,
	.clk		= "gpt7_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_per -> timer8 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer8 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer8_hwmod,
	.clk		= "gpt8_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_per -> timer9 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer9 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer9_hwmod,
	.clk		= "gpt9_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer10 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__timer10 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_timer10_hwmod,
	.clk		= "gpt10_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer11 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__timer11 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_timer11_hwmod,
	.clk		= "gpt11_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_core -> timer12 */
static struct omap_hwmod_ocp_if omap3xxx_l4_sec__timer12 = {
	.master		= &omap3xxx_l4_sec_hwmod,
	.slave		= &omap3xxx_timer12_hwmod,
	.clk		= "gpt12_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> wd_timer2 */

static struct omap_hwmod_ocp_if omap3xxx_l4_wkup__wd_timer2 = {
	.master		= &omap3xxx_l4_wkup_hwmod,
	.slave		= &omap3xxx_wd_timer2_hwmod,
	.clk		= "wdt2_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss */
static struct omap_hwmod_ocp_if omap3430es1_l4_core__dss = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3430es1_dss_core_hwmod,
	.clk		= "dss_ick",
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3ES1_L4_CORE_FW_DSS_CORE_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		},
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_core_hwmod,
	.clk		= "dss_ick",
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_CORE_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		},
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss_dispc */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss_dispc = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_dispc_hwmod,
	.clk		= "dss_ick",
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_DISPC_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		},
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss_dsi1 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss_dsi1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_dsi1_hwmod,
	.clk		= "dss_ick",
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_DSI_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		},
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss_rfbi */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss_rfbi = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_rfbi_hwmod,
	.clk		= "dss_ick",
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_RFBI_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP ,
			.flags	= OMAP_FIREWALL_L4,
		},
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss_venc */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss_venc = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_venc_hwmod,
	.clk		= "dss_ick",
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_VENC_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		},
	},
	.flags		= OCPIF_SWSUP_IDLE,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio1 */

static struct omap_hwmod_ocp_if omap3xxx_l4_wkup__gpio1 = {
	.master		= &omap3xxx_l4_wkup_hwmod,
	.slave		= &omap3xxx_gpio1_hwmod,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio2 */

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio2 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio2_hwmod,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio3 */

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio3 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio3_hwmod,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/*
 * 'mmu' class
 * The memory management unit performs virtual to physical address translation
 * for its requestors.
 */

static struct omap_hwmod_class_sysconfig mmu_sysc = {
	.rev_offs	= 0x000,
	.sysc_offs	= 0x010,
	.syss_offs	= 0x014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_mmu_hwmod_class = {
	.name = "mmu",
	.sysc = &mmu_sysc,
};

/* mmu isp */
static struct omap_hwmod omap3xxx_mmu_isp_hwmod;

/* l4_core -> mmu isp */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mmu_isp = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mmu_isp_hwmod,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod omap3xxx_mmu_isp_hwmod = {
	.name		= "mmu_isp",
	.class		= &omap3xxx_mmu_hwmod_class,
	.main_clk	= "cam_ick",
	.flags		= HWMOD_NO_IDLEST,
};

/* mmu iva */

static struct omap_hwmod omap3xxx_mmu_iva_hwmod;

static struct omap_hwmod_rst_info omap3xxx_mmu_iva_resets[] = {
	{ .name = "mmu", .rst_shift = 1, .st_shift = 9 },
};

/* l3_main -> iva mmu */
static struct omap_hwmod_ocp_if omap3xxx_l3_main__mmu_iva = {
	.master		= &omap3xxx_l3_main_hwmod,
	.slave		= &omap3xxx_mmu_iva_hwmod,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod omap3xxx_mmu_iva_hwmod = {
	.name		= "mmu_iva",
	.class		= &omap3xxx_mmu_hwmod_class,
	.clkdm_name	= "iva2_clkdm",
	.rst_lines	= omap3xxx_mmu_iva_resets,
	.rst_lines_cnt	= ARRAY_SIZE(omap3xxx_mmu_iva_resets),
	.main_clk	= "iva2_ck",
	.prcm = {
		.omap2 = {
			.module_offs = OMAP3430_IVA2_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_IVA2_SHIFT,
		},
	},
	.flags		= HWMOD_NO_IDLEST,
};

/* l4_per -> gpio4 */

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio4 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio4_hwmod,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio5 */

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio5 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio5_hwmod,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio6 */

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio6 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio6_hwmod,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dma_system -> L3 */
static struct omap_hwmod_ocp_if omap3xxx_dma_system__l3 = {
	.master		= &omap3xxx_dma_system_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "core_l3_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_cfg -> dma_system */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dma_system = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dma_system_hwmod,
	.clk		= "core_l4_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_core -> mcbsp1 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mcbsp1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mcbsp1_hwmod,
	.clk		= "mcbsp1_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_per -> mcbsp2 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp2 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp2_hwmod,
	.clk		= "mcbsp2_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_per -> mcbsp3 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp3 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp3_hwmod,
	.clk		= "mcbsp3_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_per -> mcbsp4 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp4 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp4_hwmod,
	.clk		= "mcbsp4_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_core -> mcbsp5 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mcbsp5 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mcbsp5_hwmod,
	.clk		= "mcbsp5_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


/* l4_per -> mcbsp2_sidetone */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp2_sidetone = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp2_sidetone_hwmod,
	.clk		= "mcbsp2_ick",
	.user		= OCP_USER_MPU,
};


/* l4_per -> mcbsp3_sidetone */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp3_sidetone = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp3_sidetone_hwmod,
	.clk		= "mcbsp3_ick",
	.user		= OCP_USER_MPU,
};

/* l4_core -> mailbox */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mailbox = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mailbox_hwmod,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi1 interface */
static struct omap_hwmod_ocp_if omap34xx_l4_core__mcspi1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_mcspi1,
	.clk		= "mcspi1_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi2 interface */
static struct omap_hwmod_ocp_if omap34xx_l4_core__mcspi2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_mcspi2,
	.clk		= "mcspi2_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi3 interface */
static struct omap_hwmod_ocp_if omap34xx_l4_core__mcspi3 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_mcspi3,
	.clk		= "mcspi3_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi4 interface */

static struct omap_hwmod_ocp_if omap34xx_l4_core__mcspi4 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_mcspi4,
	.clk		= "mcspi4_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if omap3xxx_usb_host_hs__l3_main_2 = {
	.master		= &omap3xxx_usb_host_hs_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "core_l3_ick",
	.user		= OCP_USER_MPU,
};


static struct omap_hwmod_ocp_if omap3xxx_l4_core__usb_host_hs = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_usb_host_hs_hwmod,
	.clk		= "usbhost_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};


static struct omap_hwmod_ocp_if omap3xxx_l4_core__usb_tll_hs = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_usb_tll_hs_hwmod,
	.clk		= "usbtll_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> hdq1w interface */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__hdq1w = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_hdq1w_hwmod,
	.clk		= "hdq_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4 | OCPIF_SWSUP_IDLE,
};

/* l4_wkup -> 32ksync_counter */


static struct omap_hwmod_ocp_if omap3xxx_l4_wkup__counter_32k = {
	.master		= &omap3xxx_l4_wkup_hwmod,
	.slave		= &omap3xxx_counter_32k_hwmod,
	.clk		= "omap_32ksync_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* am35xx has Davinci MDIO & EMAC */
static struct omap_hwmod_class am35xx_mdio_class = {
	.name = "davinci_mdio",
};

static struct omap_hwmod am35xx_mdio_hwmod = {
	.name		= "davinci_mdio",
	.class		= &am35xx_mdio_class,
	.flags		= HWMOD_NO_IDLEST,
};

/*
 * XXX Should be connected to an IPSS hwmod, not the L3 directly;
 * but this will probably require some additional hwmod core support,
 * so is left as a future to-do item.
 */
static struct omap_hwmod_ocp_if am35xx_mdio__l3 = {
	.master		= &am35xx_mdio_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "emac_fck",
	.user		= OCP_USER_MPU,
};

/* l4_core -> davinci mdio  */
/*
 * XXX Should be connected to an IPSS hwmod, not the L4_CORE directly;
 * but this will probably require some additional hwmod core support,
 * so is left as a future to-do item.
 */
static struct omap_hwmod_ocp_if am35xx_l4_core__mdio = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &am35xx_mdio_hwmod,
	.clk		= "emac_fck",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class am35xx_emac_class = {
	.name = "davinci_emac",
};

static struct omap_hwmod am35xx_emac_hwmod = {
	.name		= "davinci_emac",
	.class		= &am35xx_emac_class,
	/*
	 * According to Mark Greer, the MPU will not return from WFI
	 * when the EMAC signals an interrupt.
	 * http://www.spinics.net/lists/arm-kernel/msg174734.html
	 */
	.flags		= (HWMOD_NO_IDLEST | HWMOD_BLOCK_WFI),
};

/* l3_core -> davinci emac interface */
/*
 * XXX Should be connected to an IPSS hwmod, not the L3 directly;
 * but this will probably require some additional hwmod core support,
 * so is left as a future to-do item.
 */
static struct omap_hwmod_ocp_if am35xx_emac__l3 = {
	.master		= &am35xx_emac_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "emac_ick",
	.user		= OCP_USER_MPU,
};

/* l4_core -> davinci emac  */
/*
 * XXX Should be connected to an IPSS hwmod, not the L4_CORE directly;
 * but this will probably require some additional hwmod core support,
 * so is left as a future to-do item.
 */
static struct omap_hwmod_ocp_if am35xx_l4_core__emac = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &am35xx_emac_hwmod,
	.clk		= "emac_ick",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if omap3xxx_l3_main__gpmc = {
	.master		= &omap3xxx_l3_main_hwmod,
	.slave		= &omap3xxx_gpmc_hwmod,
	.clk		= "core_l3_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> SHAM2 (SHA1/MD5) (similar to omap24xx) */
static struct omap_hwmod_class_sysconfig omap3_sham_sysc = {
	.rev_offs	= 0x5c,
	.sysc_offs	= 0x60,
	.syss_offs	= 0x64,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.sysc_fields	= &omap3_sham_sysc_fields,
};

static struct omap_hwmod_class omap3xxx_sham_class = {
	.name	= "sham",
	.sysc	= &omap3_sham_sysc,
};



static struct omap_hwmod omap3xxx_sham_hwmod = {
	.name		= "sham",
	.main_clk	= "sha12_ick",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_SHA12_SHIFT,
		},
	},
	.class		= &omap3xxx_sham_class,
};


static struct omap_hwmod_ocp_if omap3xxx_l4_core__sham = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_sham_hwmod,
	.clk		= "sha12_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> AES */
static struct omap_hwmod_class_sysconfig omap3_aes_sysc = {
	.rev_offs	= 0x44,
	.sysc_offs	= 0x48,
	.syss_offs	= 0x4c,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap3xxx_aes_sysc_fields,
};

static struct omap_hwmod_class omap3xxx_aes_class = {
	.name	= "aes",
	.sysc	= &omap3_aes_sysc,
};


static struct omap_hwmod omap3xxx_aes_hwmod = {
	.name		= "aes",
	.main_clk	= "aes2_ick",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_AES2_SHIFT,
		},
	},
	.class		= &omap3xxx_aes_class,
};


static struct omap_hwmod_ocp_if omap3xxx_l4_core__aes = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_aes_hwmod,
	.clk		= "aes2_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/*
 * 'ssi' class
 * synchronous serial interface (multichannel and full-duplex serial if)
 */

static struct omap_hwmod_class_sysconfig omap34xx_ssi_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_AUTOIDLE | SYSC_HAS_MIDLEMODE |
			   SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_ssi_hwmod_class = {
	.name	= "ssi",
	.sysc	= &omap34xx_ssi_sysc,
};

static struct omap_hwmod omap3xxx_ssi_hwmod = {
	.name		= "ssi",
	.class		= &omap3xxx_ssi_hwmod_class,
	.clkdm_name	= "core_l4_clkdm",
	.main_clk	= "ssi_ssr_fck",
	.prcm		= {
		.omap2 = {
			.module_offs		= CORE_MOD,
			.idlest_reg_id		= 1,
			.idlest_idle_bit	= OMAP3430ES2_ST_SSI_IDLE_SHIFT,
		},
	},
};

/* L4 CORE -> SSI */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__ssi = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_ssi_hwmod,
	.clk		= "ssi_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if *omap3xxx_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l3_main__l4_core,
	&omap3xxx_l3_main__l4_per,
	&omap3xxx_mpu__l3_main,
	&omap3xxx_l3_main__l4_debugss,
	&omap3xxx_l4_core__l4_wkup,
	&omap3xxx_l4_core__mmc3,
	&omap3_l4_core__uart1,
	&omap3_l4_core__uart2,
	&omap3_l4_per__uart3,
	&omap3_l4_core__i2c1,
	&omap3_l4_core__i2c2,
	&omap3_l4_core__i2c3,
	&omap3xxx_l4_wkup__l4_sec,
	&omap3xxx_l4_wkup__timer1,
	&omap3xxx_l4_per__timer2,
	&omap3xxx_l4_per__timer3,
	&omap3xxx_l4_per__timer4,
	&omap3xxx_l4_per__timer5,
	&omap3xxx_l4_per__timer6,
	&omap3xxx_l4_per__timer7,
	&omap3xxx_l4_per__timer8,
	&omap3xxx_l4_per__timer9,
	&omap3xxx_l4_core__timer10,
	&omap3xxx_l4_core__timer11,
	&omap3xxx_l4_wkup__wd_timer2,
	&omap3xxx_l4_wkup__gpio1,
	&omap3xxx_l4_per__gpio2,
	&omap3xxx_l4_per__gpio3,
	&omap3xxx_l4_per__gpio4,
	&omap3xxx_l4_per__gpio5,
	&omap3xxx_l4_per__gpio6,
	&omap3xxx_dma_system__l3,
	&omap3xxx_l4_core__dma_system,
	&omap3xxx_l4_core__mcbsp1,
	&omap3xxx_l4_per__mcbsp2,
	&omap3xxx_l4_per__mcbsp3,
	&omap3xxx_l4_per__mcbsp4,
	&omap3xxx_l4_core__mcbsp5,
	&omap3xxx_l4_per__mcbsp2_sidetone,
	&omap3xxx_l4_per__mcbsp3_sidetone,
	&omap34xx_l4_core__mcspi1,
	&omap34xx_l4_core__mcspi2,
	&omap34xx_l4_core__mcspi3,
	&omap34xx_l4_core__mcspi4,
	&omap3xxx_l4_wkup__counter_32k,
	&omap3xxx_l3_main__gpmc,
	NULL,
};

/* GP-only hwmod links */
static struct omap_hwmod_ocp_if *omap34xx_gp_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_sec__timer12,
	NULL,
};

static struct omap_hwmod_ocp_if *omap36xx_gp_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_sec__timer12,
	NULL,
};

static struct omap_hwmod_ocp_if *am35xx_gp_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_sec__timer12,
	NULL,
};

/* crypto hwmod links */
static struct omap_hwmod_ocp_if *omap34xx_sham_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_core__sham,
	NULL,
};

static struct omap_hwmod_ocp_if *omap34xx_aes_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_core__aes,
	NULL,
};

static struct omap_hwmod_ocp_if *omap36xx_sham_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_core__sham,
	NULL
};

static struct omap_hwmod_ocp_if *omap36xx_aes_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_core__aes,
	NULL
};

/*
 * Apparently the SHA/MD5 and AES accelerator IP blocks are
 * only present on some AM35xx chips, and no one knows which
 * ones.  See
 * http://www.spinics.net/lists/arm-kernel/msg215466.html So
 * if you need these IP blocks on an AM35xx, try uncommenting
 * the following lines.
 */
static struct omap_hwmod_ocp_if *am35xx_sham_hwmod_ocp_ifs[] __initdata = {
	/* &omap3xxx_l4_core__sham, */
	NULL
};

static struct omap_hwmod_ocp_if *am35xx_aes_hwmod_ocp_ifs[] __initdata = {
	/* &omap3xxx_l4_core__aes, */
	NULL,
};

/* 3430ES1-only hwmod links */
static struct omap_hwmod_ocp_if *omap3430es1_hwmod_ocp_ifs[] __initdata = {
	&omap3430es1_dss__l3,
	&omap3430es1_l4_core__dss,
	NULL,
};

/* 3430ES2+-only hwmod links */
static struct omap_hwmod_ocp_if *omap3430es2plus_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_dss__l3,
	&omap3xxx_l4_core__dss,
	&omap3xxx_usbhsotg__l3,
	&omap3xxx_l4_core__usbhsotg,
	&omap3xxx_usb_host_hs__l3_main_2,
	&omap3xxx_l4_core__usb_host_hs,
	&omap3xxx_l4_core__usb_tll_hs,
	NULL,
};

/* <= 3430ES3-only hwmod links */
static struct omap_hwmod_ocp_if *omap3430_pre_es3_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_core__pre_es3_mmc1,
	&omap3xxx_l4_core__pre_es3_mmc2,
	NULL,
};

/* 3430ES3+-only hwmod links */
static struct omap_hwmod_ocp_if *omap3430_es3plus_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_core__es3plus_mmc1,
	&omap3xxx_l4_core__es3plus_mmc2,
	NULL,
};

/* 34xx-only hwmod links (all ES revisions) */
static struct omap_hwmod_ocp_if *omap34xx_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l3__iva,
	&omap34xx_l4_core__sr1,
	&omap34xx_l4_core__sr2,
	&omap3xxx_l4_core__mailbox,
	&omap3xxx_l4_core__hdq1w,
	&omap3xxx_sad2d__l3,
	&omap3xxx_l4_core__mmu_isp,
	&omap3xxx_l3_main__mmu_iva,
	&omap3xxx_l4_core__ssi,
	NULL,
};

/* 36xx-only hwmod links (all ES revisions) */
static struct omap_hwmod_ocp_if *omap36xx_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l3__iva,
	&omap36xx_l4_per__uart4,
	&omap3xxx_dss__l3,
	&omap3xxx_l4_core__dss,
	&omap36xx_l4_core__sr1,
	&omap36xx_l4_core__sr2,
	&omap3xxx_usbhsotg__l3,
	&omap3xxx_l4_core__usbhsotg,
	&omap3xxx_l4_core__mailbox,
	&omap3xxx_usb_host_hs__l3_main_2,
	&omap3xxx_l4_core__usb_host_hs,
	&omap3xxx_l4_core__usb_tll_hs,
	&omap3xxx_l4_core__es3plus_mmc1,
	&omap3xxx_l4_core__es3plus_mmc2,
	&omap3xxx_l4_core__hdq1w,
	&omap3xxx_sad2d__l3,
	&omap3xxx_l4_core__mmu_isp,
	&omap3xxx_l3_main__mmu_iva,
	&omap3xxx_l4_core__ssi,
	NULL,
};

static struct omap_hwmod_ocp_if *am35xx_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_dss__l3,
	&omap3xxx_l4_core__dss,
	&am35xx_usbhsotg__l3,
	&am35xx_l4_core__usbhsotg,
	&am35xx_l4_core__uart4,
	&omap3xxx_usb_host_hs__l3_main_2,
	&omap3xxx_l4_core__usb_host_hs,
	&omap3xxx_l4_core__usb_tll_hs,
	&omap3xxx_l4_core__es3plus_mmc1,
	&omap3xxx_l4_core__es3plus_mmc2,
	&omap3xxx_l4_core__hdq1w,
	&am35xx_mdio__l3,
	&am35xx_l4_core__mdio,
	&am35xx_emac__l3,
	&am35xx_l4_core__emac,
	NULL,
};

static struct omap_hwmod_ocp_if *omap3xxx_dss_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_core__dss_dispc,
	&omap3xxx_l4_core__dss_dsi1,
	&omap3xxx_l4_core__dss_rfbi,
	&omap3xxx_l4_core__dss_venc,
	NULL,
};

/**
 * omap3xxx_hwmod_is_hs_ip_block_usable - is a security IP block accessible?
 * @bus: struct device_node * for the top-level OMAP DT data
 * @dev_name: device name used in the DT file
 *
 * Determine whether a "secure" IP block @dev_name is usable by Linux.
 * There doesn't appear to be a 100% reliable way to determine this,
 * so we rely on heuristics.  If @bus is null, meaning there's no DT
 * data, then we only assume the IP block is accessible if the OMAP is
 * fused as a 'general-purpose' SoC.  If however DT data is present,
 * test to see if the IP block is described in the DT data and set to
 * 'status = "okay"'.  If so then we assume the ODM has configured the
 * OMAP firewalls to allow access to the IP block.
 *
 * Return: 0 if device named @dev_name is not likely to be accessible,
 * or 1 if it is likely to be accessible.
 */
static bool __init omap3xxx_hwmod_is_hs_ip_block_usable(struct device_node *bus,
							const char *dev_name)
{
	struct device_node *node;
	bool available;

	if (!bus)
		return omap_type() == OMAP2_DEVICE_TYPE_GP;

	node = of_get_child_by_name(bus, dev_name);
	available = of_device_is_available(node);
	of_node_put(node);

	return available;
}

int __init omap3xxx_hwmod_init(void)
{
	int r;
	struct omap_hwmod_ocp_if **h = NULL, **h_gp = NULL, **h_sham = NULL;
	struct omap_hwmod_ocp_if **h_aes = NULL;
	struct device_node *bus;
	unsigned int rev;

	omap_hwmod_init();

	/* Register hwmod links common to all OMAP3 */
	r = omap_hwmod_register_links(omap3xxx_hwmod_ocp_ifs);
	if (r < 0)
		return r;

	rev = omap_rev();

	/*
	 * Register hwmod links common to individual OMAP3 families, all
	 * silicon revisions (e.g., 34xx, or AM3505/3517, or 36xx)
	 * All possible revisions should be included in this conditional.
	 */
	if (rev == OMAP3430_REV_ES1_0 || rev == OMAP3430_REV_ES2_0 ||
	    rev == OMAP3430_REV_ES2_1 || rev == OMAP3430_REV_ES3_0 ||
	    rev == OMAP3430_REV_ES3_1 || rev == OMAP3430_REV_ES3_1_2) {
		h = omap34xx_hwmod_ocp_ifs;
		h_gp = omap34xx_gp_hwmod_ocp_ifs;
		h_sham = omap34xx_sham_hwmod_ocp_ifs;
		h_aes = omap34xx_aes_hwmod_ocp_ifs;
	} else if (rev == AM35XX_REV_ES1_0 || rev == AM35XX_REV_ES1_1) {
		h = am35xx_hwmod_ocp_ifs;
		h_gp = am35xx_gp_hwmod_ocp_ifs;
		h_sham = am35xx_sham_hwmod_ocp_ifs;
		h_aes = am35xx_aes_hwmod_ocp_ifs;
	} else if (rev == OMAP3630_REV_ES1_0 || rev == OMAP3630_REV_ES1_1 ||
		   rev == OMAP3630_REV_ES1_2) {
		h = omap36xx_hwmod_ocp_ifs;
		h_gp = omap36xx_gp_hwmod_ocp_ifs;
		h_sham = omap36xx_sham_hwmod_ocp_ifs;
		h_aes = omap36xx_aes_hwmod_ocp_ifs;
	} else {
		WARN(1, "OMAP3 hwmod family init: unknown chip type\n");
		return -EINVAL;
	}

	r = omap_hwmod_register_links(h);
	if (r < 0)
		return r;

	/* Register GP-only hwmod links. */
	if (h_gp && omap_type() == OMAP2_DEVICE_TYPE_GP) {
		r = omap_hwmod_register_links(h_gp);
		if (r < 0)
			return r;
	}

	/*
	 * Register crypto hwmod links only if they are not disabled in DT.
	 * If DT information is missing, enable them only for GP devices.
	 */

	bus = of_find_node_by_name(NULL, "ocp");

	if (h_sham && omap3xxx_hwmod_is_hs_ip_block_usable(bus, "sham")) {
		r = omap_hwmod_register_links(h_sham);
		if (r < 0)
			goto put_node;
	}

	if (h_aes && omap3xxx_hwmod_is_hs_ip_block_usable(bus, "aes")) {
		r = omap_hwmod_register_links(h_aes);
		if (r < 0)
			goto put_node;
	}
	of_node_put(bus);

	/*
	 * Register hwmod links specific to certain ES levels of a
	 * particular family of silicon (e.g., 34xx ES1.0)
	 */
	h = NULL;
	if (rev == OMAP3430_REV_ES1_0) {
		h = omap3430es1_hwmod_ocp_ifs;
	} else if (rev == OMAP3430_REV_ES2_0 || rev == OMAP3430_REV_ES2_1 ||
		   rev == OMAP3430_REV_ES3_0 || rev == OMAP3430_REV_ES3_1 ||
		   rev == OMAP3430_REV_ES3_1_2) {
		h = omap3430es2plus_hwmod_ocp_ifs;
	}

	if (h) {
		r = omap_hwmod_register_links(h);
		if (r < 0)
			return r;
	}

	h = NULL;
	if (rev == OMAP3430_REV_ES1_0 || rev == OMAP3430_REV_ES2_0 ||
	    rev == OMAP3430_REV_ES2_1) {
		h = omap3430_pre_es3_hwmod_ocp_ifs;
	} else if (rev == OMAP3430_REV_ES3_0 || rev == OMAP3430_REV_ES3_1 ||
		   rev == OMAP3430_REV_ES3_1_2) {
		h = omap3430_es3plus_hwmod_ocp_ifs;
	}

	if (h)
		r = omap_hwmod_register_links(h);
	if (r < 0)
		return r;

	/*
	 * DSS code presumes that dss_core hwmod is handled first,
	 * _before_ any other DSS related hwmods so register common
	 * DSS hwmod links last to ensure that dss_core is already
	 * registered.  Otherwise some change things may happen, for
	 * ex. if dispc is handled before dss_core and DSS is enabled
	 * in bootloader DISPC will be reset with outputs enabled
	 * which sometimes leads to unrecoverable L3 error.  XXX The
	 * long-term fix to this is to ensure hwmods are set up in
	 * dependency order in the hwmod core code.
	 */
	r = omap_hwmod_register_links(omap3xxx_dss_hwmod_ocp_ifs);

	return r;

put_node:
	of_node_put(bus);
	return r;
}
