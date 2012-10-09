/*
 * omap_hwmod_2xxx_ipblock_data.c - common IP block data for OMAP2xxx
 *
 * Copyright (C) 2011 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <plat/omap_hwmod.h>
#include <plat/serial.h>
#include <linux/platform_data/gpio-omap.h>
#include <plat/dma.h>
#include <plat/dmtimer.h>
#include <linux/platform_data/spi-omap2-mcspi.h>

#include "omap_hwmod_common_data.h"
#include "cm-regbits-24xx.h"
#include "prm-regbits-24xx.h"
#include "wd_timer.h"

struct omap_hwmod_irq_info omap2xxx_timer12_mpu_irqs[] = {
	{ .irq = 48 + OMAP_INTC_START, },
	{ .irq = -1 },
};

struct omap_hwmod_dma_info omap2xxx_dss_sdma_chs[] = {
	{ .name = "dispc", .dma_req = 5 },
	{ .dma_req = -1 }
};

/*
 * 'dispc' class
 * display controller
 */

static struct omap_hwmod_class_sysconfig omap2_dispc_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			   MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2_dispc_hwmod_class = {
	.name	= "dispc",
	.sysc	= &omap2_dispc_sysc,
};

/* OMAP2xxx Timer Common */
static struct omap_hwmod_class_sysconfig omap2xxx_timer_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_CLOCKACTIVITY |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2xxx_timer_hwmod_class = {
	.name	= "timer",
	.sysc	= &omap2xxx_timer_sysc,
};

/*
 * 'wd_timer' class
 * 32-bit watchdog upward counter that generates a pulse on the reset pin on
 * overflow condition
 */

static struct omap_hwmod_class_sysconfig omap2xxx_wd_timer_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_EMUFREE | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2xxx_wd_timer_hwmod_class = {
	.name		= "wd_timer",
	.sysc		= &omap2xxx_wd_timer_sysc,
	.pre_shutdown	= &omap2_wd_timer_disable,
	.reset		= &omap2_wd_timer_reset,
};

/*
 * 'gpio' class
 * general purpose io module
 */
static struct omap_hwmod_class_sysconfig omap2xxx_gpio_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_ENAWAKEUP | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2xxx_gpio_hwmod_class = {
	.name = "gpio",
	.sysc = &omap2xxx_gpio_sysc,
	.rev = 0,
};

/* system dma */
static struct omap_hwmod_class_sysconfig omap2xxx_dma_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x002c,
	.syss_offs	= 0x0028,
	.sysc_flags	= (SYSC_HAS_SOFTRESET | SYSC_HAS_MIDLEMODE |
			   SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_EMUFREE |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2xxx_dma_hwmod_class = {
	.name	= "dma",
	.sysc	= &omap2xxx_dma_sysc,
};

/*
 * 'mailbox' class
 * mailbox module allowing communication between the on-chip processors
 * using a queued mailbox-interrupt mechanism.
 */

static struct omap_hwmod_class_sysconfig omap2xxx_mailbox_sysc = {
	.rev_offs	= 0x000,
	.sysc_offs	= 0x010,
	.syss_offs	= 0x014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2xxx_mailbox_hwmod_class = {
	.name	= "mailbox",
	.sysc	= &omap2xxx_mailbox_sysc,
};

/*
 * 'mcspi' class
 * multichannel serial port interface (mcspi) / master/slave synchronous serial
 * bus
 */

static struct omap_hwmod_class_sysconfig omap2xxx_mcspi_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
				SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

struct omap_hwmod_class omap2xxx_mcspi_class = {
	.name	= "mcspi",
	.sysc	= &omap2xxx_mcspi_sysc,
	.rev	= OMAP2_MCSPI_REV,
};

/*
 * 'gpmc' class
 * general purpose memory controller
 */

static struct omap_hwmod_class_sysconfig omap2xxx_gpmc_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_AUTOIDLE | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2xxx_gpmc_hwmod_class = {
	.name	= "gpmc",
	.sysc	= &omap2xxx_gpmc_sysc,
};

/*
 * IP blocks
 */

/* L3 */
struct omap_hwmod omap2xxx_l3_main_hwmod = {
	.name		= "l3_main",
	.class		= &l3_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

/* L4 CORE */
struct omap_hwmod omap2xxx_l4_core_hwmod = {
	.name		= "l4_core",
	.class		= &l4_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

/* L4 WKUP */
struct omap_hwmod omap2xxx_l4_wkup_hwmod = {
	.name		= "l4_wkup",
	.class		= &l4_hwmod_class,
	.flags		= HWMOD_NO_IDLEST,
};

/* MPU */
static struct omap_hwmod_irq_info omap2xxx_mpu_irqs[] = {
	{ .name = "pmu", .irq = 3 + OMAP_INTC_START },
	{ .irq = -1 }
};

struct omap_hwmod omap2xxx_mpu_hwmod = {
	.name		= "mpu",
	.mpu_irqs	= omap2xxx_mpu_irqs,
	.class		= &mpu_hwmod_class,
	.main_clk	= "mpu_ck",
};

/* IVA2 */
struct omap_hwmod omap2xxx_iva_hwmod = {
	.name		= "iva",
	.class		= &iva_hwmod_class,
};

/* always-on timers dev attribute */
static struct omap_timer_capability_dev_attr capability_alwon_dev_attr = {
	.timer_capability       = OMAP_TIMER_ALWON,
};

/* pwm timers dev attribute */
static struct omap_timer_capability_dev_attr capability_pwm_dev_attr = {
	.timer_capability       = OMAP_TIMER_HAS_PWM,
};

/* timers with DSP interrupt dev attribute */
static struct omap_timer_capability_dev_attr capability_dsp_dev_attr = {
	.timer_capability       = OMAP_TIMER_HAS_DSP_IRQ,
};

/* timer1 */

struct omap_hwmod omap2xxx_timer1_hwmod = {
	.name		= "timer1",
	.mpu_irqs	= omap2_timer1_mpu_irqs,
	.main_clk	= "gpt1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT1_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT1_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer2 */

struct omap_hwmod omap2xxx_timer2_hwmod = {
	.name		= "timer2",
	.mpu_irqs	= omap2_timer2_mpu_irqs,
	.main_clk	= "gpt2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT2_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT2_SHIFT,
		},
	},
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer3 */

struct omap_hwmod omap2xxx_timer3_hwmod = {
	.name		= "timer3",
	.mpu_irqs	= omap2_timer3_mpu_irqs,
	.main_clk	= "gpt3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT3_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT3_SHIFT,
		},
	},
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer4 */

struct omap_hwmod omap2xxx_timer4_hwmod = {
	.name		= "timer4",
	.mpu_irqs	= omap2_timer4_mpu_irqs,
	.main_clk	= "gpt4_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT4_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT4_SHIFT,
		},
	},
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer5 */

struct omap_hwmod omap2xxx_timer5_hwmod = {
	.name		= "timer5",
	.mpu_irqs	= omap2_timer5_mpu_irqs,
	.main_clk	= "gpt5_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT5_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT5_SHIFT,
		},
	},
	.dev_attr	= &capability_dsp_dev_attr,
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer6 */

struct omap_hwmod omap2xxx_timer6_hwmod = {
	.name		= "timer6",
	.mpu_irqs	= omap2_timer6_mpu_irqs,
	.main_clk	= "gpt6_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT6_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT6_SHIFT,
		},
	},
	.dev_attr	= &capability_dsp_dev_attr,
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer7 */

struct omap_hwmod omap2xxx_timer7_hwmod = {
	.name		= "timer7",
	.mpu_irqs	= omap2_timer7_mpu_irqs,
	.main_clk	= "gpt7_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT7_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT7_SHIFT,
		},
	},
	.dev_attr	= &capability_dsp_dev_attr,
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer8 */

struct omap_hwmod omap2xxx_timer8_hwmod = {
	.name		= "timer8",
	.mpu_irqs	= omap2_timer8_mpu_irqs,
	.main_clk	= "gpt8_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT8_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT8_SHIFT,
		},
	},
	.dev_attr	= &capability_dsp_dev_attr,
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer9 */

struct omap_hwmod omap2xxx_timer9_hwmod = {
	.name		= "timer9",
	.mpu_irqs	= omap2_timer9_mpu_irqs,
	.main_clk	= "gpt9_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT9_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT9_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer10 */

struct omap_hwmod omap2xxx_timer10_hwmod = {
	.name		= "timer10",
	.mpu_irqs	= omap2_timer10_mpu_irqs,
	.main_clk	= "gpt10_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT10_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT10_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer11 */

struct omap_hwmod omap2xxx_timer11_hwmod = {
	.name		= "timer11",
	.mpu_irqs	= omap2_timer11_mpu_irqs,
	.main_clk	= "gpt11_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT11_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT11_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer12 */

struct omap_hwmod omap2xxx_timer12_hwmod = {
	.name		= "timer12",
	.mpu_irqs	= omap2xxx_timer12_mpu_irqs,
	.main_clk	= "gpt12_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT12_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT12_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.class		= &omap2xxx_timer_hwmod_class,
};

/* wd_timer2 */
struct omap_hwmod omap2xxx_wd_timer2_hwmod = {
	.name		= "wd_timer2",
	.class		= &omap2xxx_wd_timer_hwmod_class,
	.main_clk	= "mpu_wdt_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MPU_WDT_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MPU_WDT_SHIFT,
		},
	},
};

/* UART1 */

struct omap_hwmod omap2xxx_uart1_hwmod = {
	.name		= "uart1",
	.mpu_irqs	= omap2_uart1_mpu_irqs,
	.sdma_reqs	= omap2_uart1_sdma_reqs,
	.main_clk	= "uart1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_UART1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_EN_UART1_SHIFT,
		},
	},
	.class		= &omap2_uart_class,
};

/* UART2 */

struct omap_hwmod omap2xxx_uart2_hwmod = {
	.name		= "uart2",
	.mpu_irqs	= omap2_uart2_mpu_irqs,
	.sdma_reqs	= omap2_uart2_sdma_reqs,
	.main_clk	= "uart2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_UART2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_EN_UART2_SHIFT,
		},
	},
	.class		= &omap2_uart_class,
};

/* UART3 */

struct omap_hwmod omap2xxx_uart3_hwmod = {
	.name		= "uart3",
	.mpu_irqs	= omap2_uart3_mpu_irqs,
	.sdma_reqs	= omap2_uart3_sdma_reqs,
	.main_clk	= "uart3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 2,
			.module_bit = OMAP24XX_EN_UART3_SHIFT,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP24XX_EN_UART3_SHIFT,
		},
	},
	.class		= &omap2_uart_class,
};

/* dss */

static struct omap_hwmod_opt_clk dss_opt_clks[] = {
	/*
	 * The DSS HW needs all DSS clocks enabled during reset. The dss_core
	 * driver does not use these clocks.
	 */
	{ .role = "tv_clk", .clk = "dss_54m_fck" },
	{ .role = "sys_clk", .clk = "dss2_fck" },
};

struct omap_hwmod omap2xxx_dss_core_hwmod = {
	.name		= "dss_core",
	.class		= &omap2_dss_hwmod_class,
	.main_clk	= "dss1_fck", /* instead of dss_fck */
	.sdma_reqs	= omap2xxx_dss_sdma_chs,
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_DSS1_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_stdby_bit = OMAP24XX_ST_DSS_SHIFT,
		},
	},
	.opt_clks	= dss_opt_clks,
	.opt_clks_cnt = ARRAY_SIZE(dss_opt_clks),
	.flags		= HWMOD_NO_IDLEST | HWMOD_CONTROL_OPT_CLKS_IN_RESET,
};

struct omap_hwmod omap2xxx_dss_dispc_hwmod = {
	.name		= "dss_dispc",
	.class		= &omap2_dispc_hwmod_class,
	.mpu_irqs	= omap2_dispc_irqs,
	.main_clk	= "dss1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_DSS1_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_stdby_bit = OMAP24XX_ST_DSS_SHIFT,
		},
	},
	.flags		= HWMOD_NO_IDLEST,
	.dev_attr	= &omap2_3_dss_dispc_dev_attr
};

static struct omap_hwmod_opt_clk dss_rfbi_opt_clks[] = {
	{ .role = "ick", .clk = "dss_ick" },
};

struct omap_hwmod omap2xxx_dss_rfbi_hwmod = {
	.name		= "dss_rfbi",
	.class		= &omap2_rfbi_hwmod_class,
	.main_clk	= "dss1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_DSS1_SHIFT,
			.module_offs = CORE_MOD,
		},
	},
	.opt_clks	= dss_rfbi_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dss_rfbi_opt_clks),
	.flags		= HWMOD_NO_IDLEST,
};

struct omap_hwmod omap2xxx_dss_venc_hwmod = {
	.name		= "dss_venc",
	.class		= &omap2_venc_hwmod_class,
	.main_clk	= "dss_54m_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_DSS1_SHIFT,
			.module_offs = CORE_MOD,
		},
	},
	.flags		= HWMOD_NO_IDLEST,
};

/* gpio dev_attr */
struct omap_gpio_dev_attr omap2xxx_gpio_dev_attr = {
	.bank_width = 32,
	.dbck_flag = false,
};

/* gpio1 */
struct omap_hwmod omap2xxx_gpio1_hwmod = {
	.name		= "gpio1",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio1_irqs,
	.main_clk	= "gpios_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPIOS_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPIOS_SHIFT,
		},
	},
	.class		= &omap2xxx_gpio_hwmod_class,
	.dev_attr	= &omap2xxx_gpio_dev_attr,
};

/* gpio2 */
struct omap_hwmod omap2xxx_gpio2_hwmod = {
	.name		= "gpio2",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio2_irqs,
	.main_clk	= "gpios_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPIOS_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPIOS_SHIFT,
		},
	},
	.class		= &omap2xxx_gpio_hwmod_class,
	.dev_attr	= &omap2xxx_gpio_dev_attr,
};

/* gpio3 */
struct omap_hwmod omap2xxx_gpio3_hwmod = {
	.name		= "gpio3",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio3_irqs,
	.main_clk	= "gpios_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPIOS_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPIOS_SHIFT,
		},
	},
	.class		= &omap2xxx_gpio_hwmod_class,
	.dev_attr	= &omap2xxx_gpio_dev_attr,
};

/* gpio4 */
struct omap_hwmod omap2xxx_gpio4_hwmod = {
	.name		= "gpio4",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio4_irqs,
	.main_clk	= "gpios_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPIOS_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPIOS_SHIFT,
		},
	},
	.class		= &omap2xxx_gpio_hwmod_class,
	.dev_attr	= &omap2xxx_gpio_dev_attr,
};

/* mcspi1 */
static struct omap2_mcspi_dev_attr omap_mcspi1_dev_attr = {
	.num_chipselect = 4,
};

struct omap_hwmod omap2xxx_mcspi1_hwmod = {
	.name		= "mcspi1",
	.mpu_irqs	= omap2_mcspi1_mpu_irqs,
	.sdma_reqs	= omap2_mcspi1_sdma_reqs,
	.main_clk	= "mcspi1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MCSPI1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCSPI1_SHIFT,
		},
	},
	.class		= &omap2xxx_mcspi_class,
	.dev_attr	= &omap_mcspi1_dev_attr,
};

/* mcspi2 */
static struct omap2_mcspi_dev_attr omap_mcspi2_dev_attr = {
	.num_chipselect = 2,
};

struct omap_hwmod omap2xxx_mcspi2_hwmod = {
	.name		= "mcspi2",
	.mpu_irqs	= omap2_mcspi2_mpu_irqs,
	.sdma_reqs	= omap2_mcspi2_sdma_reqs,
	.main_clk	= "mcspi2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MCSPI2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCSPI2_SHIFT,
		},
	},
	.class		= &omap2xxx_mcspi_class,
	.dev_attr	= &omap_mcspi2_dev_attr,
};

static struct omap_hwmod_class omap2xxx_counter_hwmod_class = {
	.name	= "counter",
};

struct omap_hwmod omap2xxx_counter_32k_hwmod = {
	.name		= "counter_32k",
	.main_clk	= "func_32k_ck",
	.prcm		= {
		.omap2	= {
			.module_offs = WKUP_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_ST_32KSYNC_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_32KSYNC_SHIFT,
		},
	},
	.class		= &omap2xxx_counter_hwmod_class,
};

/* gpmc */
static struct omap_hwmod_irq_info omap2xxx_gpmc_irqs[] = {
	{ .irq = 20 },
	{ .irq = -1 }
};

struct omap_hwmod omap2xxx_gpmc_hwmod = {
	.name		= "gpmc",
	.class		= &omap2xxx_gpmc_hwmod_class,
	.mpu_irqs	= omap2xxx_gpmc_irqs,
	.main_clk	= "gpmc_fck",
	/*
	 * XXX HWMOD_INIT_NO_RESET should not be needed for this IP
	 * block.  It is not being added due to any known bugs with
	 * resetting the GPMC IP block, but rather because any timings
	 * set by the bootloader are not being correctly programmed by
	 * the kernel from the board file or DT data.
	 * HWMOD_INIT_NO_RESET should be removed ASAP.
	 */
	.flags		= (HWMOD_INIT_NO_IDLE | HWMOD_INIT_NO_RESET |
			   HWMOD_NO_IDLEST),
	.prcm		= {
		.omap2	= {
			.prcm_reg_id = 3,
			.module_bit = OMAP24XX_EN_GPMC_MASK,
			.module_offs = CORE_MOD,
		},
	},
};

/* RNG */

static struct omap_hwmod_class_sysconfig omap2_rng_sysc = {
	.rev_offs	= 0x3c,
	.sysc_offs	= 0x40,
	.syss_offs	= 0x44,
	.sysc_flags	= (SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2_rng_hwmod_class = {
	.name		= "rng",
	.sysc		= &omap2_rng_sysc,
};

static struct omap_hwmod_irq_info omap2_rng_mpu_irqs[] = {
	{ .irq = 52 },
	{ .irq = -1 }
};

struct omap_hwmod omap2xxx_rng_hwmod = {
	.name		= "rng",
	.mpu_irqs	= omap2_rng_mpu_irqs,
	.main_clk	= "l4_ck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 4,
			.module_bit = OMAP24XX_EN_RNG_SHIFT,
			.idlest_reg_id = 4,
			.idlest_idle_bit = OMAP24XX_ST_RNG_SHIFT,
		},
	},
	/*
	 * XXX The first read from the SYSSTATUS register of the RNG
	 * after the SYSCONFIG SOFTRESET bit is set triggers an
	 * imprecise external abort.  It's unclear why this happens.
	 * Until this is analyzed, skip the IP block reset.
	 */
	.flags		= HWMOD_INIT_NO_RESET,
	.class		= &omap2_rng_hwmod_class,
};
