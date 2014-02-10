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

#include <linux/i2c-omap.h>
#include <linux/power/smartreflex.h>
#include <linux/platform_data/gpio-omap.h>

#include <linux/omap-dma.h>
#include "l3_3xxx.h"
#include "l4_3xxx.h"
#include <linux/platform_data/asoc-ti-mcbsp.h>
#include <linux/platform_data/spi-omap2-mcspi.h>
#include <linux/platform_data/iommu-omap.h>
#include <plat/dmtimer.h>

#include "am35xx.h"

#include "soc.h"
#include "omap_hwmod.h"
#include "omap_hwmod_common_data.h"
#include "prm-regbits-34xx.h"
#include "cm-regbits-34xx.h"

#include "dma.h"
#include "i2c.h"
#include "mmc.h"
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

/*
 * IP blocks
 */

/* L3 */
static struct omap_hwmod_irq_info omap3xxx_l3_main_irqs[] = {
	{ .irq = 9 + OMAP_INTC_START, },
	{ .irq = 10 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap3xxx_l3_main_hwmod = {
	.name		= "l3_main",
	.class		= &l3_hwmod_class,
	.mpu_irqs	= omap3xxx_l3_main_irqs,
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
static struct omap_hwmod_irq_info omap3xxx_mpu_irqs[] = {
	{ .name = "pmu", .irq = 3 + OMAP_INTC_START },
	{ .irq = -1 }
};

static struct omap_hwmod omap3xxx_mpu_hwmod = {
	.name		= "mpu",
	.mpu_irqs	= omap3xxx_mpu_irqs,
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
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_CM_FCLKEN_IVA2_EN_IVA2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_IVA2_SHIFT,
		}
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
	.clockact	= CLOCKACT_TEST_ICLK,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_timer_hwmod_class = {
	.name = "timer",
	.sysc = &omap3xxx_timer_sysc,
};

/* secure timers dev attribute */
static struct omap_timer_capability_dev_attr capability_secure_dev_attr = {
	.timer_capability	= OMAP_TIMER_ALWON | OMAP_TIMER_SECURE,
};

/* always-on timers dev attribute */
static struct omap_timer_capability_dev_attr capability_alwon_dev_attr = {
	.timer_capability	= OMAP_TIMER_ALWON,
};

/* pwm timers dev attribute */
static struct omap_timer_capability_dev_attr capability_pwm_dev_attr = {
	.timer_capability	= OMAP_TIMER_HAS_PWM,
};

/* timers with DSP interrupt dev attribute */
static struct omap_timer_capability_dev_attr capability_dsp_dev_attr = {
	.timer_capability       = OMAP_TIMER_HAS_DSP_IRQ,
};

/* pwm timers with DSP interrupt dev attribute */
static struct omap_timer_capability_dev_attr capability_dsp_pwm_dev_attr = {
	.timer_capability       = OMAP_TIMER_HAS_DSP_IRQ | OMAP_TIMER_HAS_PWM,
};

/* timer1 */
static struct omap_hwmod omap3xxx_timer1_hwmod = {
	.name		= "timer1",
	.mpu_irqs	= omap2_timer1_mpu_irqs,
	.main_clk	= "gpt1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT1_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT1_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer2 */
static struct omap_hwmod omap3xxx_timer2_hwmod = {
	.name		= "timer2",
	.mpu_irqs	= omap2_timer2_mpu_irqs,
	.main_clk	= "gpt2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT2_SHIFT,
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
	.mpu_irqs	= omap2_timer3_mpu_irqs,
	.main_clk	= "gpt3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT3_SHIFT,
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
	.mpu_irqs	= omap2_timer4_mpu_irqs,
	.main_clk	= "gpt4_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT4_SHIFT,
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
	.mpu_irqs	= omap2_timer5_mpu_irqs,
	.main_clk	= "gpt5_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT5_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT5_SHIFT,
		},
	},
	.dev_attr	= &capability_dsp_dev_attr,
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer6 */
static struct omap_hwmod omap3xxx_timer6_hwmod = {
	.name		= "timer6",
	.mpu_irqs	= omap2_timer6_mpu_irqs,
	.main_clk	= "gpt6_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT6_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT6_SHIFT,
		},
	},
	.dev_attr	= &capability_dsp_dev_attr,
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer7 */
static struct omap_hwmod omap3xxx_timer7_hwmod = {
	.name		= "timer7",
	.mpu_irqs	= omap2_timer7_mpu_irqs,
	.main_clk	= "gpt7_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT7_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT7_SHIFT,
		},
	},
	.dev_attr	= &capability_dsp_dev_attr,
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer8 */
static struct omap_hwmod omap3xxx_timer8_hwmod = {
	.name		= "timer8",
	.mpu_irqs	= omap2_timer8_mpu_irqs,
	.main_clk	= "gpt8_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT8_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT8_SHIFT,
		},
	},
	.dev_attr	= &capability_dsp_pwm_dev_attr,
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer9 */
static struct omap_hwmod omap3xxx_timer9_hwmod = {
	.name		= "timer9",
	.mpu_irqs	= omap2_timer9_mpu_irqs,
	.main_clk	= "gpt9_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT9_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT9_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer10 */
static struct omap_hwmod omap3xxx_timer10_hwmod = {
	.name		= "timer10",
	.mpu_irqs	= omap2_timer10_mpu_irqs,
	.main_clk	= "gpt10_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT10_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT10_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer11 */
static struct omap_hwmod omap3xxx_timer11_hwmod = {
	.name		= "timer11",
	.mpu_irqs	= omap2_timer11_mpu_irqs,
	.main_clk	= "gpt11_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT11_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT11_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.class		= &omap3xxx_timer_hwmod_class,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

/* timer12 */
static struct omap_hwmod_irq_info omap3xxx_timer12_mpu_irqs[] = {
	{ .irq = 95 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap3xxx_timer12_hwmod = {
	.name		= "timer12",
	.mpu_irqs	= omap3xxx_timer12_mpu_irqs,
	.main_clk	= "gpt12_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT12_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT12_SHIFT,
		},
	},
	.dev_attr	= &capability_secure_dev_attr,
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
	.clockact	= CLOCKACT_TEST_ICLK,
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
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_WDT2_SHIFT,
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
	.mpu_irqs	= omap2_uart1_mpu_irqs,
	.sdma_reqs	= omap2_uart1_sdma_reqs,
	.main_clk	= "uart1_fck",
	.flags		= HWMOD_SWSUP_SIDLE_ACT,
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_UART1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_UART1_SHIFT,
		},
	},
	.class		= &omap2_uart_class,
};

/* UART2 */
static struct omap_hwmod omap3xxx_uart2_hwmod = {
	.name		= "uart2",
	.mpu_irqs	= omap2_uart2_mpu_irqs,
	.sdma_reqs	= omap2_uart2_sdma_reqs,
	.main_clk	= "uart2_fck",
	.flags		= HWMOD_SWSUP_SIDLE_ACT,
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_UART2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_UART2_SHIFT,
		},
	},
	.class		= &omap2_uart_class,
};

/* UART3 */
static struct omap_hwmod omap3xxx_uart3_hwmod = {
	.name		= "uart3",
	.mpu_irqs	= omap2_uart3_mpu_irqs,
	.sdma_reqs	= omap2_uart3_sdma_reqs,
	.main_clk	= "uart3_fck",
	.flags		= HWMOD_SWSUP_SIDLE_ACT,
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_UART3_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_UART3_SHIFT,
		},
	},
	.class		= &omap2_uart_class,
};

/* UART4 */
static struct omap_hwmod_irq_info uart4_mpu_irqs[] = {
	{ .irq = 80 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info uart4_sdma_reqs[] = {
	{ .name = "rx",	.dma_req = OMAP36XX_DMA_UART4_RX, },
	{ .name = "tx",	.dma_req = OMAP36XX_DMA_UART4_TX, },
	{ .dma_req = -1 }
};

static struct omap_hwmod omap36xx_uart4_hwmod = {
	.name		= "uart4",
	.mpu_irqs	= uart4_mpu_irqs,
	.sdma_reqs	= uart4_sdma_reqs,
	.main_clk	= "uart4_fck",
	.flags		= HWMOD_SWSUP_SIDLE_ACT,
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3630_EN_UART4_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3630_EN_UART4_SHIFT,
		},
	},
	.class		= &omap2_uart_class,
};

static struct omap_hwmod_irq_info am35xx_uart4_mpu_irqs[] = {
	{ .irq = 84 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info am35xx_uart4_sdma_reqs[] = {
	{ .name = "rx", .dma_req = AM35XX_DMA_UART4_RX, },
	{ .name = "tx", .dma_req = AM35XX_DMA_UART4_TX, },
	{ .dma_req = -1 }
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
	.mpu_irqs	= am35xx_uart4_mpu_irqs,
	.sdma_reqs	= am35xx_uart4_sdma_reqs,
	.main_clk	= "uart4_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = AM35XX_EN_UART4_SHIFT,
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

static struct omap_hwmod_dma_info omap3xxx_dss_sdma_chs[] = {
	{ .name = "dispc", .dma_req = 5 },
	{ .name = "dsi1", .dma_req = 74 },
	{ .dma_req = -1 }
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
	.sdma_reqs	= omap3xxx_dss_sdma_chs,
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_DSS1_SHIFT,
			.module_offs = OMAP3430_DSS_MOD,
			.idlest_reg_id = 1,
			.idlest_stdby_bit = OMAP3430ES1_ST_DSS_SHIFT,
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
	.sdma_reqs	= omap3xxx_dss_sdma_chs,
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_DSS1_SHIFT,
			.module_offs = OMAP3430_DSS_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430ES2_ST_DSS_IDLE_SHIFT,
			.idlest_stdby_bit = OMAP3430ES2_ST_DSS_STDBY_SHIFT,
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
	.mpu_irqs	= omap2_dispc_irqs,
	.main_clk	= "dss1_alwon_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_DSS1_SHIFT,
			.module_offs = OMAP3430_DSS_MOD,
		},
	},
	.flags		= HWMOD_NO_IDLEST,
	.dev_attr	= &omap2_3_dss_dispc_dev_attr
};

/*
 * 'dsi' class
 * display serial interface controller
 */

static struct omap_hwmod_class omap3xxx_dsi_hwmod_class = {
	.name = "dsi",
};

static struct omap_hwmod_irq_info omap3xxx_dsi1_irqs[] = {
	{ .irq = 25 + OMAP_INTC_START, },
	{ .irq = -1 },
};

/* dss_dsi1 */
static struct omap_hwmod_opt_clk dss_dsi1_opt_clks[] = {
	{ .role = "sys_clk", .clk = "dss2_alwon_fck" },
};

static struct omap_hwmod omap3xxx_dss_dsi1_hwmod = {
	.name		= "dss_dsi1",
	.class		= &omap3xxx_dsi_hwmod_class,
	.mpu_irqs	= omap3xxx_dsi1_irqs,
	.main_clk	= "dss1_alwon_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_DSS1_SHIFT,
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
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_DSS1_SHIFT,
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
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_DSS1_SHIFT,
			.module_offs = OMAP3430_DSS_MOD,
		},
	},
	.opt_clks	= dss_venc_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dss_venc_opt_clks),
	.flags		= HWMOD_NO_IDLEST,
};

/* I2C1 */
static struct omap_i2c_dev_attr i2c1_dev_attr = {
	.fifo_depth	= 8, /* bytes */
	.flags		= OMAP_I2C_FLAG_BUS_SHIFT_2,
};

static struct omap_hwmod omap3xxx_i2c1_hwmod = {
	.name		= "i2c1",
	.flags		= HWMOD_16BIT_REG | HWMOD_SET_DEFAULT_CLOCKACT,
	.mpu_irqs	= omap2_i2c1_mpu_irqs,
	.sdma_reqs	= omap2_i2c1_sdma_reqs,
	.main_clk	= "i2c1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_I2C1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_I2C1_SHIFT,
		},
	},
	.class		= &i2c_class,
	.dev_attr	= &i2c1_dev_attr,
};

/* I2C2 */
static struct omap_i2c_dev_attr i2c2_dev_attr = {
	.fifo_depth	= 8, /* bytes */
	.flags = OMAP_I2C_FLAG_BUS_SHIFT_2,
};

static struct omap_hwmod omap3xxx_i2c2_hwmod = {
	.name		= "i2c2",
	.flags		= HWMOD_16BIT_REG | HWMOD_SET_DEFAULT_CLOCKACT,
	.mpu_irqs	= omap2_i2c2_mpu_irqs,
	.sdma_reqs	= omap2_i2c2_sdma_reqs,
	.main_clk	= "i2c2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_I2C2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_I2C2_SHIFT,
		},
	},
	.class		= &i2c_class,
	.dev_attr	= &i2c2_dev_attr,
};

/* I2C3 */
static struct omap_i2c_dev_attr i2c3_dev_attr = {
	.fifo_depth	= 64, /* bytes */
	.flags = OMAP_I2C_FLAG_BUS_SHIFT_2,
};

static struct omap_hwmod_irq_info i2c3_mpu_irqs[] = {
	{ .irq = 61 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info i2c3_sdma_reqs[] = {
	{ .name = "tx", .dma_req = OMAP34XX_DMA_I2C3_TX },
	{ .name = "rx", .dma_req = OMAP34XX_DMA_I2C3_RX },
	{ .dma_req = -1 }
};

static struct omap_hwmod omap3xxx_i2c3_hwmod = {
	.name		= "i2c3",
	.flags		= HWMOD_16BIT_REG | HWMOD_SET_DEFAULT_CLOCKACT,
	.mpu_irqs	= i2c3_mpu_irqs,
	.sdma_reqs	= i2c3_sdma_reqs,
	.main_clk	= "i2c3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_I2C3_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_I2C3_SHIFT,
		},
	},
	.class		= &i2c_class,
	.dev_attr	= &i2c3_dev_attr,
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

/* gpio_dev_attr */
static struct omap_gpio_dev_attr gpio_dev_attr = {
	.bank_width = 32,
	.dbck_flag = true,
};

/* gpio1 */
static struct omap_hwmod_opt_clk gpio1_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio1_dbck", },
};

static struct omap_hwmod omap3xxx_gpio1_hwmod = {
	.name		= "gpio1",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio1_irqs,
	.main_clk	= "gpio1_ick",
	.opt_clks	= gpio1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio1_opt_clks),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPIO1_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO1_SHIFT,
		},
	},
	.class		= &omap3xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio2 */
static struct omap_hwmod_opt_clk gpio2_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio2_dbck", },
};

static struct omap_hwmod omap3xxx_gpio2_hwmod = {
	.name		= "gpio2",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio2_irqs,
	.main_clk	= "gpio2_ick",
	.opt_clks	= gpio2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio2_opt_clks),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPIO2_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO2_SHIFT,
		},
	},
	.class		= &omap3xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio3 */
static struct omap_hwmod_opt_clk gpio3_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio3_dbck", },
};

static struct omap_hwmod omap3xxx_gpio3_hwmod = {
	.name		= "gpio3",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio3_irqs,
	.main_clk	= "gpio3_ick",
	.opt_clks	= gpio3_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio3_opt_clks),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPIO3_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO3_SHIFT,
		},
	},
	.class		= &omap3xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio4 */
static struct omap_hwmod_opt_clk gpio4_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio4_dbck", },
};

static struct omap_hwmod omap3xxx_gpio4_hwmod = {
	.name		= "gpio4",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio4_irqs,
	.main_clk	= "gpio4_ick",
	.opt_clks	= gpio4_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio4_opt_clks),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPIO4_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO4_SHIFT,
		},
	},
	.class		= &omap3xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio5 */
static struct omap_hwmod_irq_info omap3xxx_gpio5_irqs[] = {
	{ .irq = 33 + OMAP_INTC_START, }, /* INT_34XX_GPIO_BANK5 */
	{ .irq = -1 },
};

static struct omap_hwmod_opt_clk gpio5_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio5_dbck", },
};

static struct omap_hwmod omap3xxx_gpio5_hwmod = {
	.name		= "gpio5",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap3xxx_gpio5_irqs,
	.main_clk	= "gpio5_ick",
	.opt_clks	= gpio5_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio5_opt_clks),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPIO5_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO5_SHIFT,
		},
	},
	.class		= &omap3xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio6 */
static struct omap_hwmod_irq_info omap3xxx_gpio6_irqs[] = {
	{ .irq = 34 + OMAP_INTC_START, }, /* INT_34XX_GPIO_BANK6 */
	{ .irq = -1 },
};

static struct omap_hwmod_opt_clk gpio6_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio6_dbck", },
};

static struct omap_hwmod omap3xxx_gpio6_hwmod = {
	.name		= "gpio6",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap3xxx_gpio6_irqs,
	.main_clk	= "gpio6_ick",
	.opt_clks	= gpio6_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio6_opt_clks),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPIO6_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO6_SHIFT,
		},
	},
	.class		= &omap3xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
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
	.mpu_irqs	= omap2_dma_system_irqs,
	.main_clk	= "core_l3_ick",
	.prcm = {
		.omap2 = {
			.module_offs		= CORE_MOD,
			.prcm_reg_id		= 1,
			.module_bit		= OMAP3430_ST_SDMA_SHIFT,
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
	.sysc_offs	= 0x008c,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_ENAWAKEUP |
			   SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
	.clockact	= 0x2,
};

static struct omap_hwmod_class omap3xxx_mcbsp_hwmod_class = {
	.name = "mcbsp",
	.sysc = &omap3xxx_mcbsp_sysc,
	.rev  = MCBSP_CONFIG_TYPE3,
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
static struct omap_hwmod_irq_info omap3xxx_mcbsp1_irqs[] = {
	{ .name = "common", .irq = 16 + OMAP_INTC_START, },
	{ .name = "tx", .irq = 59 + OMAP_INTC_START, },
	{ .name = "rx", .irq = 60 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap3xxx_mcbsp1_hwmod = {
	.name		= "mcbsp1",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp1_irqs,
	.sdma_reqs	= omap2_mcbsp1_sdma_reqs,
	.main_clk	= "mcbsp1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCBSP1_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP1_SHIFT,
		},
	},
	.opt_clks	= mcbsp15_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp15_opt_clks),
};

/* mcbsp2 */
static struct omap_hwmod_irq_info omap3xxx_mcbsp2_irqs[] = {
	{ .name = "common", .irq = 17 + OMAP_INTC_START, },
	{ .name = "tx", .irq = 62 + OMAP_INTC_START, },
	{ .name = "rx", .irq = 63 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_mcbsp_dev_attr omap34xx_mcbsp2_dev_attr = {
	.sidetone	= "mcbsp2_sidetone",
};

static struct omap_hwmod omap3xxx_mcbsp2_hwmod = {
	.name		= "mcbsp2",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp2_irqs,
	.sdma_reqs	= omap2_mcbsp2_sdma_reqs,
	.main_clk	= "mcbsp2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCBSP2_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP2_SHIFT,
		},
	},
	.opt_clks	= mcbsp234_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp234_opt_clks),
	.dev_attr	= &omap34xx_mcbsp2_dev_attr,
};

/* mcbsp3 */
static struct omap_hwmod_irq_info omap3xxx_mcbsp3_irqs[] = {
	{ .name = "common", .irq = 22 + OMAP_INTC_START, },
	{ .name = "tx", .irq = 89 + OMAP_INTC_START, },
	{ .name = "rx", .irq = 90 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_mcbsp_dev_attr omap34xx_mcbsp3_dev_attr = {
	.sidetone	= "mcbsp3_sidetone",
};

static struct omap_hwmod omap3xxx_mcbsp3_hwmod = {
	.name		= "mcbsp3",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp3_irqs,
	.sdma_reqs	= omap2_mcbsp3_sdma_reqs,
	.main_clk	= "mcbsp3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCBSP3_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP3_SHIFT,
		},
	},
	.opt_clks	= mcbsp234_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp234_opt_clks),
	.dev_attr	= &omap34xx_mcbsp3_dev_attr,
};

/* mcbsp4 */
static struct omap_hwmod_irq_info omap3xxx_mcbsp4_irqs[] = {
	{ .name = "common", .irq = 23 + OMAP_INTC_START, },
	{ .name = "tx", .irq = 54 + OMAP_INTC_START, },
	{ .name = "rx", .irq = 55 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info omap3xxx_mcbsp4_sdma_chs[] = {
	{ .name = "rx", .dma_req = 20 },
	{ .name = "tx", .dma_req = 19 },
	{ .dma_req = -1 }
};

static struct omap_hwmod omap3xxx_mcbsp4_hwmod = {
	.name		= "mcbsp4",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp4_irqs,
	.sdma_reqs	= omap3xxx_mcbsp4_sdma_chs,
	.main_clk	= "mcbsp4_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCBSP4_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP4_SHIFT,
		},
	},
	.opt_clks	= mcbsp234_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(mcbsp234_opt_clks),
};

/* mcbsp5 */
static struct omap_hwmod_irq_info omap3xxx_mcbsp5_irqs[] = {
	{ .name = "common", .irq = 27 + OMAP_INTC_START, },
	{ .name = "tx", .irq = 81 + OMAP_INTC_START, },
	{ .name = "rx", .irq = 82 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info omap3xxx_mcbsp5_sdma_chs[] = {
	{ .name = "rx", .dma_req = 22 },
	{ .name = "tx", .dma_req = 21 },
	{ .dma_req = -1 }
};

static struct omap_hwmod omap3xxx_mcbsp5_hwmod = {
	.name		= "mcbsp5",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp5_irqs,
	.sdma_reqs	= omap3xxx_mcbsp5_sdma_chs,
	.main_clk	= "mcbsp5_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCBSP5_SHIFT,
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
	.sysc_offs	= 0x0010,
	.sysc_flags	= SYSC_HAS_AUTOIDLE,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_mcbsp_sidetone_hwmod_class = {
	.name = "mcbsp_sidetone",
	.sysc = &omap3xxx_mcbsp_sidetone_sysc,
};

/* mcbsp2_sidetone */
static struct omap_hwmod_irq_info omap3xxx_mcbsp2_sidetone_irqs[] = {
	{ .name = "irq", .irq = 4 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap3xxx_mcbsp2_sidetone_hwmod = {
	.name		= "mcbsp2_sidetone",
	.class		= &omap3xxx_mcbsp_sidetone_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp2_sidetone_irqs,
	.main_clk	= "mcbsp2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			 .module_bit = OMAP3430_EN_MCBSP2_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP2_SHIFT,
		},
	},
};

/* mcbsp3_sidetone */
static struct omap_hwmod_irq_info omap3xxx_mcbsp3_sidetone_irqs[] = {
	{ .name = "irq", .irq = 5 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap3xxx_mcbsp3_sidetone_hwmod = {
	.name		= "mcbsp3_sidetone",
	.class		= &omap3xxx_mcbsp_sidetone_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp3_sidetone_irqs,
	.main_clk	= "mcbsp3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCBSP3_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP3_SHIFT,
		},
	},
};

/* SR common */
static struct omap_hwmod_sysc_fields omap34xx_sr_sysc_fields = {
	.clkact_shift	= 20,
};

static struct omap_hwmod_class_sysconfig omap34xx_sr_sysc = {
	.sysc_offs	= 0x24,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_NO_CACHE),
	.clockact	= CLOCKACT_TEST_ICLK,
	.sysc_fields	= &omap34xx_sr_sysc_fields,
};

static struct omap_hwmod_class omap34xx_smartreflex_hwmod_class = {
	.name = "smartreflex",
	.sysc = &omap34xx_sr_sysc,
	.rev  = 1,
};

static struct omap_hwmod_sysc_fields omap36xx_sr_sysc_fields = {
	.sidle_shift	= 24,
	.enwkup_shift	= 26,
};

static struct omap_hwmod_class_sysconfig omap36xx_sr_sysc = {
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

static struct omap_hwmod_irq_info omap3_smartreflex_mpu_irqs[] = {
	{ .irq = 18 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap34xx_sr1_hwmod = {
	.name		= "smartreflex_mpu_iva",
	.class		= &omap34xx_smartreflex_hwmod_class,
	.main_clk	= "sr1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_SR1_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_SR1_SHIFT,
		},
	},
	.dev_attr	= &sr1_dev_attr,
	.mpu_irqs	= omap3_smartreflex_mpu_irqs,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

static struct omap_hwmod omap36xx_sr1_hwmod = {
	.name		= "smartreflex_mpu_iva",
	.class		= &omap36xx_smartreflex_hwmod_class,
	.main_clk	= "sr1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_SR1_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_SR1_SHIFT,
		},
	},
	.dev_attr	= &sr1_dev_attr,
	.mpu_irqs	= omap3_smartreflex_mpu_irqs,
};

/* SR2 */
static struct omap_smartreflex_dev_attr sr2_dev_attr = {
	.sensor_voltdm_name	= "core",
};

static struct omap_hwmod_irq_info omap3_smartreflex_core_irqs[] = {
	{ .irq = 19 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap34xx_sr2_hwmod = {
	.name		= "smartreflex_core",
	.class		= &omap34xx_smartreflex_hwmod_class,
	.main_clk	= "sr2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_SR2_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_SR2_SHIFT,
		},
	},
	.dev_attr	= &sr2_dev_attr,
	.mpu_irqs	= omap3_smartreflex_core_irqs,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

static struct omap_hwmod omap36xx_sr2_hwmod = {
	.name		= "smartreflex_core",
	.class		= &omap36xx_smartreflex_hwmod_class,
	.main_clk	= "sr2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_SR2_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_SR2_SHIFT,
		},
	},
	.dev_attr	= &sr2_dev_attr,
	.mpu_irqs	= omap3_smartreflex_core_irqs,
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

static struct omap_hwmod_irq_info omap3xxx_mailbox_irqs[] = {
	{ .irq = 26 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap3xxx_mailbox_hwmod = {
	.name		= "mailbox",
	.class		= &omap3xxx_mailbox_hwmod_class,
	.mpu_irqs	= omap3xxx_mailbox_irqs,
	.main_clk	= "mailboxes_ick",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MAILBOXES_SHIFT,
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
	.rev = OMAP3_MCSPI_REV,
};

/* mcspi1 */
static struct omap2_mcspi_dev_attr omap_mcspi1_dev_attr = {
	.num_chipselect = 4,
};

static struct omap_hwmod omap34xx_mcspi1 = {
	.name		= "mcspi1",
	.mpu_irqs	= omap2_mcspi1_mpu_irqs,
	.sdma_reqs	= omap2_mcspi1_sdma_reqs,
	.main_clk	= "mcspi1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCSPI1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCSPI1_SHIFT,
		},
	},
	.class		= &omap34xx_mcspi_class,
	.dev_attr       = &omap_mcspi1_dev_attr,
};

/* mcspi2 */
static struct omap2_mcspi_dev_attr omap_mcspi2_dev_attr = {
	.num_chipselect = 2,
};

static struct omap_hwmod omap34xx_mcspi2 = {
	.name		= "mcspi2",
	.mpu_irqs	= omap2_mcspi2_mpu_irqs,
	.sdma_reqs	= omap2_mcspi2_sdma_reqs,
	.main_clk	= "mcspi2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCSPI2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCSPI2_SHIFT,
		},
	},
	.class		= &omap34xx_mcspi_class,
	.dev_attr       = &omap_mcspi2_dev_attr,
};

/* mcspi3 */
static struct omap_hwmod_irq_info omap34xx_mcspi3_mpu_irqs[] = {
	{ .name = "irq", .irq = 91 + OMAP_INTC_START, }, /* 91 */
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info omap34xx_mcspi3_sdma_reqs[] = {
	{ .name = "tx0", .dma_req = 15 },
	{ .name = "rx0", .dma_req = 16 },
	{ .name = "tx1", .dma_req = 23 },
	{ .name = "rx1", .dma_req = 24 },
	{ .dma_req = -1 }
};

static struct omap2_mcspi_dev_attr omap_mcspi3_dev_attr = {
	.num_chipselect = 2,
};

static struct omap_hwmod omap34xx_mcspi3 = {
	.name		= "mcspi3",
	.mpu_irqs	= omap34xx_mcspi3_mpu_irqs,
	.sdma_reqs	= omap34xx_mcspi3_sdma_reqs,
	.main_clk	= "mcspi3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCSPI3_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCSPI3_SHIFT,
		},
	},
	.class		= &omap34xx_mcspi_class,
	.dev_attr       = &omap_mcspi3_dev_attr,
};

/* mcspi4 */
static struct omap_hwmod_irq_info omap34xx_mcspi4_mpu_irqs[] = {
	{ .name = "irq", .irq = 48 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info omap34xx_mcspi4_sdma_reqs[] = {
	{ .name = "tx0", .dma_req = 70 }, /* DMA_SPI4_TX0 */
	{ .name = "rx0", .dma_req = 71 }, /* DMA_SPI4_RX0 */
	{ .dma_req = -1 }
};

static struct omap2_mcspi_dev_attr omap_mcspi4_dev_attr = {
	.num_chipselect = 1,
};

static struct omap_hwmod omap34xx_mcspi4 = {
	.name		= "mcspi4",
	.mpu_irqs	= omap34xx_mcspi4_mpu_irqs,
	.sdma_reqs	= omap34xx_mcspi4_sdma_reqs,
	.main_clk	= "mcspi4_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCSPI4_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCSPI4_SHIFT,
		},
	},
	.class		= &omap34xx_mcspi_class,
	.dev_attr       = &omap_mcspi4_dev_attr,
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
static struct omap_hwmod_irq_info omap3xxx_usbhsotg_mpu_irqs[] = {

	{ .name = "mc", .irq = 92 + OMAP_INTC_START, },
	{ .name = "dma", .irq = 93 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap3xxx_usbhsotg_hwmod = {
	.name		= "usb_otg_hs",
	.mpu_irqs	= omap3xxx_usbhsotg_mpu_irqs,
	.main_clk	= "hsotgusb_ick",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_HSOTGUSB_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430ES2_ST_HSOTGUSB_IDLE_SHIFT,
			.idlest_stdby_bit = OMAP3430ES2_ST_HSOTGUSB_STDBY_SHIFT
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
	.flags		= HWMOD_NO_OCP_AUTOIDLE | HWMOD_SWSUP_SIDLE
				| HWMOD_FORCE_MSTANDBY,
};

/* usb_otg_hs */
static struct omap_hwmod_irq_info am35xx_usbhsotg_mpu_irqs[] = {
	{ .name = "mc", .irq = 71 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_class am35xx_usbotg_class = {
	.name = "am35xx_usbotg",
};

static struct omap_hwmod am35xx_usbhsotg_hwmod = {
	.name		= "am35x_otg_hs",
	.mpu_irqs	= am35xx_usbhsotg_mpu_irqs,
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

static struct omap_hwmod_irq_info omap34xx_mmc1_mpu_irqs[] = {
	{ .irq = 83 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info omap34xx_mmc1_sdma_reqs[] = {
	{ .name = "tx",	.dma_req = 61, },
	{ .name = "rx",	.dma_req = 62, },
	{ .dma_req = -1 }
};

static struct omap_hwmod_opt_clk omap34xx_mmc1_opt_clks[] = {
	{ .role = "dbck", .clk = "omap_32k_fck", },
};

static struct omap_mmc_dev_attr mmc1_dev_attr = {
	.flags = OMAP_HSMMC_SUPPORTS_DUAL_VOLT,
};

/* See 35xx errata 2.1.1.128 in SPRZ278F */
static struct omap_mmc_dev_attr mmc1_pre_es3_dev_attr = {
	.flags = (OMAP_HSMMC_SUPPORTS_DUAL_VOLT |
		  OMAP_HSMMC_BROKEN_MULTIBLOCK_READ),
};

static struct omap_hwmod omap3xxx_pre_es3_mmc1_hwmod = {
	.name		= "mmc1",
	.mpu_irqs	= omap34xx_mmc1_mpu_irqs,
	.sdma_reqs	= omap34xx_mmc1_sdma_reqs,
	.opt_clks	= omap34xx_mmc1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc1_opt_clks),
	.main_clk	= "mmchs1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MMC1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC1_SHIFT,
		},
	},
	.dev_attr	= &mmc1_pre_es3_dev_attr,
	.class		= &omap34xx_mmc_class,
};

static struct omap_hwmod omap3xxx_es3plus_mmc1_hwmod = {
	.name		= "mmc1",
	.mpu_irqs	= omap34xx_mmc1_mpu_irqs,
	.sdma_reqs	= omap34xx_mmc1_sdma_reqs,
	.opt_clks	= omap34xx_mmc1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc1_opt_clks),
	.main_clk	= "mmchs1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MMC1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC1_SHIFT,
		},
	},
	.dev_attr	= &mmc1_dev_attr,
	.class		= &omap34xx_mmc_class,
};

/* MMC/SD/SDIO2 */

static struct omap_hwmod_irq_info omap34xx_mmc2_mpu_irqs[] = {
	{ .irq = 86 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info omap34xx_mmc2_sdma_reqs[] = {
	{ .name = "tx",	.dma_req = 47, },
	{ .name = "rx",	.dma_req = 48, },
	{ .dma_req = -1 }
};

static struct omap_hwmod_opt_clk omap34xx_mmc2_opt_clks[] = {
	{ .role = "dbck", .clk = "omap_32k_fck", },
};

/* See 35xx errata 2.1.1.128 in SPRZ278F */
static struct omap_mmc_dev_attr mmc2_pre_es3_dev_attr = {
	.flags = OMAP_HSMMC_BROKEN_MULTIBLOCK_READ,
};

static struct omap_hwmod omap3xxx_pre_es3_mmc2_hwmod = {
	.name		= "mmc2",
	.mpu_irqs	= omap34xx_mmc2_mpu_irqs,
	.sdma_reqs	= omap34xx_mmc2_sdma_reqs,
	.opt_clks	= omap34xx_mmc2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc2_opt_clks),
	.main_clk	= "mmchs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MMC2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC2_SHIFT,
		},
	},
	.dev_attr	= &mmc2_pre_es3_dev_attr,
	.class		= &omap34xx_mmc_class,
};

static struct omap_hwmod omap3xxx_es3plus_mmc2_hwmod = {
	.name		= "mmc2",
	.mpu_irqs	= omap34xx_mmc2_mpu_irqs,
	.sdma_reqs	= omap34xx_mmc2_sdma_reqs,
	.opt_clks	= omap34xx_mmc2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc2_opt_clks),
	.main_clk	= "mmchs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MMC2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC2_SHIFT,
		},
	},
	.class		= &omap34xx_mmc_class,
};

/* MMC/SD/SDIO3 */

static struct omap_hwmod_irq_info omap34xx_mmc3_mpu_irqs[] = {
	{ .irq = 94 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod_dma_info omap34xx_mmc3_sdma_reqs[] = {
	{ .name = "tx",	.dma_req = 77, },
	{ .name = "rx",	.dma_req = 78, },
	{ .dma_req = -1 }
};

static struct omap_hwmod_opt_clk omap34xx_mmc3_opt_clks[] = {
	{ .role = "dbck", .clk = "omap_32k_fck", },
};

static struct omap_hwmod omap3xxx_mmc3_hwmod = {
	.name		= "mmc3",
	.mpu_irqs	= omap34xx_mmc3_mpu_irqs,
	.sdma_reqs	= omap34xx_mmc3_sdma_reqs,
	.opt_clks	= omap34xx_mmc3_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc3_opt_clks),
	.main_clk	= "mmchs3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MMC3_SHIFT,
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

static struct omap_hwmod_opt_clk omap3xxx_usb_host_hs_opt_clks[] = {
	  { .role = "ehci_logic_fck", .clk = "usbhost_120m_fck", },
};

static struct omap_hwmod_irq_info omap3xxx_usb_host_hs_irqs[] = {
	{ .name = "ohci-irq", .irq = 76 + OMAP_INTC_START, },
	{ .name = "ehci-irq", .irq = 77 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap3xxx_usb_host_hs_hwmod = {
	.name		= "usb_host_hs",
	.class		= &omap3xxx_usb_host_hs_hwmod_class,
	.clkdm_name	= "l3_init_clkdm",
	.mpu_irqs	= omap3xxx_usb_host_hs_irqs,
	.main_clk	= "usbhost_48m_fck",
	.prcm = {
		.omap2 = {
			.module_offs = OMAP3430ES2_USBHOST_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430ES2_EN_USBHOST1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430ES2_ST_USBHOST_IDLE_SHIFT,
			.idlest_stdby_bit = OMAP3430ES2_ST_USBHOST_STDBY_SHIFT,
		},
	},
	.opt_clks	= omap3xxx_usb_host_hs_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap3xxx_usb_host_hs_opt_clks),

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

static struct omap_hwmod_irq_info omap3xxx_usb_tll_hs_irqs[] = {
	{ .name = "tll-irq", .irq = 78 + OMAP_INTC_START, },
	{ .irq = -1 },
};

static struct omap_hwmod omap3xxx_usb_tll_hs_hwmod = {
	.name		= "usb_tll_hs",
	.class		= &omap3xxx_usb_tll_hs_hwmod_class,
	.clkdm_name	= "l3_init_clkdm",
	.mpu_irqs	= omap3xxx_usb_tll_hs_irqs,
	.main_clk	= "usbtll_fck",
	.prcm = {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 3,
			.module_bit = OMAP3430ES2_EN_USBTLL_SHIFT,
			.idlest_reg_id = 3,
			.idlest_idle_bit = OMAP3430ES2_ST_USBTLL_SHIFT,
		},
	},
};

static struct omap_hwmod omap3xxx_hdq1w_hwmod = {
	.name		= "hdq1w",
	.mpu_irqs	= omap2_hdq1w_mpu_irqs,
	.main_clk	= "hdq_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_HDQ_SHIFT,
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
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_SAD2D_SHIFT,
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
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_ST_32KSYNC_SHIFT,
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

static struct omap_hwmod_irq_info omap3xxx_gpmc_irqs[] = {
	{ .irq = 20 + OMAP_INTC_START, },
	{ .irq = -1 }
};

static struct omap_hwmod omap3xxx_gpmc_hwmod = {
	.name		= "gpmc",
	.class		= &omap3xxx_gpmc_hwmod_class,
	.clkdm_name	= "core_l3_clkdm",
	.mpu_irqs	= omap3xxx_gpmc_irqs,
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

static struct omap_hwmod_addr_space omap3xxx_l3_main_addrs[] = {
	{
		.pa_start	= 0x68000000,
		.pa_end		= 0x6800ffff,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

/* MPU -> L3 interface */
static struct omap_hwmod_ocp_if omap3xxx_mpu__l3_main = {
	.master   = &omap3xxx_mpu_hwmod,
	.slave    = &omap3xxx_l3_main_hwmod,
	.addr     = omap3xxx_l3_main_addrs,
	.user	= OCP_USER_MPU,
};

static struct omap_hwmod_addr_space omap3xxx_l4_emu_addrs[] = {
	{
		.pa_start	= 0x54000000,
		.pa_end		= 0x547fffff,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

/* l3 -> debugss */
static struct omap_hwmod_ocp_if omap3xxx_l3_main__l4_debugss = {
	.master		= &omap3xxx_l3_main_hwmod,
	.slave		= &omap3xxx_debugss_hwmod,
	.addr		= omap3xxx_l4_emu_addrs,
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
		}
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
	.addr		= omap2430_mmc1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__es3plus_mmc1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_es3plus_mmc1_hwmod,
	.clk		= "mmchs1_ick",
	.addr		= omap2430_mmc1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4
};

/* L4 CORE -> MMC2 interface */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__pre_es3_mmc2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_pre_es3_mmc2_hwmod,
	.clk		= "mmchs2_ick",
	.addr		= omap2430_mmc2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__es3plus_mmc2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_es3plus_mmc2_hwmod,
	.clk		= "mmchs2_ick",
	.addr		= omap2430_mmc2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4
};

/* L4 CORE -> MMC3 interface */
static struct omap_hwmod_addr_space omap3xxx_mmc3_addr_space[] = {
	{
		.pa_start	= 0x480ad000,
		.pa_end		= 0x480ad1ff,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__mmc3 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mmc3_hwmod,
	.clk		= "mmchs3_ick",
	.addr		= omap3xxx_mmc3_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4
};

/* L4 CORE -> UART1 interface */
static struct omap_hwmod_addr_space omap3xxx_uart1_addr_space[] = {
	{
		.pa_start	= OMAP3_UART1_BASE,
		.pa_end		= OMAP3_UART1_BASE + SZ_8K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3_l4_core__uart1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_uart1_hwmod,
	.clk		= "uart1_ick",
	.addr		= omap3xxx_uart1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> UART2 interface */
static struct omap_hwmod_addr_space omap3xxx_uart2_addr_space[] = {
	{
		.pa_start	= OMAP3_UART2_BASE,
		.pa_end		= OMAP3_UART2_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3_l4_core__uart2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_uart2_hwmod,
	.clk		= "uart2_ick",
	.addr		= omap3xxx_uart2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 PER -> UART3 interface */
static struct omap_hwmod_addr_space omap3xxx_uart3_addr_space[] = {
	{
		.pa_start	= OMAP3_UART3_BASE,
		.pa_end		= OMAP3_UART3_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3_l4_per__uart3 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_uart3_hwmod,
	.clk		= "uart3_ick",
	.addr		= omap3xxx_uart3_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 PER -> UART4 interface */
static struct omap_hwmod_addr_space omap36xx_uart4_addr_space[] = {
	{
		.pa_start	= OMAP3_UART4_BASE,
		.pa_end		= OMAP3_UART4_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap36xx_l4_per__uart4 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap36xx_uart4_hwmod,
	.clk		= "uart4_ick",
	.addr		= omap36xx_uart4_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* AM35xx: L4 CORE -> UART4 interface */
static struct omap_hwmod_addr_space am35xx_uart4_addr_space[] = {
	{
		.pa_start	= OMAP3_UART4_AM35XX_BASE,
		.pa_end		= OMAP3_UART4_AM35XX_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if am35xx_l4_core__uart4 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &am35xx_uart4_hwmod,
	.clk		= "uart4_ick",
	.addr		= am35xx_uart4_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C1 interface */
static struct omap_hwmod_ocp_if omap3_l4_core__i2c1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_i2c1_hwmod,
	.clk		= "i2c1_ick",
	.addr		= omap2_i2c1_addr_space,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_I2C1_REGION,
			.l4_prot_group = 7,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C2 interface */
static struct omap_hwmod_ocp_if omap3_l4_core__i2c2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_i2c2_hwmod,
	.clk		= "i2c2_ick",
	.addr		= omap2_i2c2_addr_space,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_I2C2_REGION,
			.l4_prot_group = 7,
			.flags = OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C3 interface */
static struct omap_hwmod_addr_space omap3xxx_i2c3_addr_space[] = {
	{
		.pa_start	= 0x48060000,
		.pa_end		= 0x48060000 + SZ_128 - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3_l4_core__i2c3 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_i2c3_hwmod,
	.clk		= "i2c3_ick",
	.addr		= omap3xxx_i2c3_addr_space,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_I2C3_REGION,
			.l4_prot_group = 7,
			.flags = OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> SR1 interface */
static struct omap_hwmod_addr_space omap3_sr1_addr_space[] = {
	{
		.pa_start	= OMAP34XX_SR1_BASE,
		.pa_end		= OMAP34XX_SR1_BASE + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap34xx_l4_core__sr1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_sr1_hwmod,
	.clk		= "sr_l4_ick",
	.addr		= omap3_sr1_addr_space,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if omap36xx_l4_core__sr1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap36xx_sr1_hwmod,
	.clk		= "sr_l4_ick",
	.addr		= omap3_sr1_addr_space,
	.user		= OCP_USER_MPU,
};

/* L4 CORE -> SR1 interface */
static struct omap_hwmod_addr_space omap3_sr2_addr_space[] = {
	{
		.pa_start	= OMAP34XX_SR2_BASE,
		.pa_end		= OMAP34XX_SR2_BASE + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap34xx_l4_core__sr2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_sr2_hwmod,
	.clk		= "sr_l4_ick",
	.addr		= omap3_sr2_addr_space,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if omap36xx_l4_core__sr2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap36xx_sr2_hwmod,
	.clk		= "sr_l4_ick",
	.addr		= omap3_sr2_addr_space,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_addr_space omap3xxx_usbhsotg_addrs[] = {
	{
		.pa_start	= OMAP34XX_HSUSB_OTG_BASE,
		.pa_end		= OMAP34XX_HSUSB_OTG_BASE + SZ_4K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> usbhsotg  */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__usbhsotg = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_usbhsotg_hwmod,
	.clk		= "l4_ick",
	.addr		= omap3xxx_usbhsotg_addrs,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_addr_space am35xx_usbhsotg_addrs[] = {
	{
		.pa_start	= AM35XX_IPSS_USBOTGSS_BASE,
		.pa_end		= AM35XX_IPSS_USBOTGSS_BASE + SZ_4K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> usbhsotg  */
static struct omap_hwmod_ocp_if am35xx_l4_core__usbhsotg = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &am35xx_usbhsotg_hwmod,
	.clk		= "hsotgusb_ick",
	.addr		= am35xx_usbhsotg_addrs,
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

static struct omap_hwmod_addr_space omap3xxx_timer1_addrs[] = {
	{
		.pa_start	= 0x48318000,
		.pa_end		= 0x48318000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_wkup -> timer1 */
static struct omap_hwmod_ocp_if omap3xxx_l4_wkup__timer1 = {
	.master		= &omap3xxx_l4_wkup_hwmod,
	.slave		= &omap3xxx_timer1_hwmod,
	.clk		= "gpt1_ick",
	.addr		= omap3xxx_timer1_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_timer2_addrs[] = {
	{
		.pa_start	= 0x49032000,
		.pa_end		= 0x49032000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer2 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer2 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer2_hwmod,
	.clk		= "gpt2_ick",
	.addr		= omap3xxx_timer2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_timer3_addrs[] = {
	{
		.pa_start	= 0x49034000,
		.pa_end		= 0x49034000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer3 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer3 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer3_hwmod,
	.clk		= "gpt3_ick",
	.addr		= omap3xxx_timer3_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_timer4_addrs[] = {
	{
		.pa_start	= 0x49036000,
		.pa_end		= 0x49036000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer4 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer4 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer4_hwmod,
	.clk		= "gpt4_ick",
	.addr		= omap3xxx_timer4_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_timer5_addrs[] = {
	{
		.pa_start	= 0x49038000,
		.pa_end		= 0x49038000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer5 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer5 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer5_hwmod,
	.clk		= "gpt5_ick",
	.addr		= omap3xxx_timer5_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_timer6_addrs[] = {
	{
		.pa_start	= 0x4903A000,
		.pa_end		= 0x4903A000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer6 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer6 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer6_hwmod,
	.clk		= "gpt6_ick",
	.addr		= omap3xxx_timer6_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_timer7_addrs[] = {
	{
		.pa_start	= 0x4903C000,
		.pa_end		= 0x4903C000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer7 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer7 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer7_hwmod,
	.clk		= "gpt7_ick",
	.addr		= omap3xxx_timer7_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_timer8_addrs[] = {
	{
		.pa_start	= 0x4903E000,
		.pa_end		= 0x4903E000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer8 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer8 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer8_hwmod,
	.clk		= "gpt8_ick",
	.addr		= omap3xxx_timer8_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_timer9_addrs[] = {
	{
		.pa_start	= 0x49040000,
		.pa_end		= 0x49040000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer9 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer9 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer9_hwmod,
	.clk		= "gpt9_ick",
	.addr		= omap3xxx_timer9_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer10 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__timer10 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_timer10_hwmod,
	.clk		= "gpt10_ick",
	.addr		= omap2_timer10_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer11 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__timer11 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_timer11_hwmod,
	.clk		= "gpt11_ick",
	.addr		= omap2_timer11_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_timer12_addrs[] = {
	{
		.pa_start	= 0x48304000,
		.pa_end		= 0x48304000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> timer12 */
static struct omap_hwmod_ocp_if omap3xxx_l4_sec__timer12 = {
	.master		= &omap3xxx_l4_sec_hwmod,
	.slave		= &omap3xxx_timer12_hwmod,
	.clk		= "gpt12_ick",
	.addr		= omap3xxx_timer12_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> wd_timer2 */
static struct omap_hwmod_addr_space omap3xxx_wd_timer2_addrs[] = {
	{
		.pa_start	= 0x48314000,
		.pa_end		= 0x4831407f,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_wkup__wd_timer2 = {
	.master		= &omap3xxx_l4_wkup_hwmod,
	.slave		= &omap3xxx_wd_timer2_hwmod,
	.clk		= "wdt2_ick",
	.addr		= omap3xxx_wd_timer2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss */
static struct omap_hwmod_ocp_if omap3430es1_l4_core__dss = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3430es1_dss_core_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3ES1_L4_CORE_FW_DSS_CORE_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_core_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_CORE_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss_dispc */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss_dispc = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_dispc_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_dispc_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_DISPC_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_dss_dsi1_addrs[] = {
	{
		.pa_start	= 0x4804FC00,
		.pa_end		= 0x4804FFFF,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> dss_dsi1 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss_dsi1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_dsi1_hwmod,
	.clk		= "dss_ick",
	.addr		= omap3xxx_dss_dsi1_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_DSI_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss_rfbi */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss_rfbi = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_rfbi_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_rfbi_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_RFBI_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP ,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss_venc */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss_venc = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_venc_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_venc_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_VENC_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.flags		= OCPIF_SWSUP_IDLE,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio1 */
static struct omap_hwmod_addr_space omap3xxx_gpio1_addrs[] = {
	{
		.pa_start	= 0x48310000,
		.pa_end		= 0x483101ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_wkup__gpio1 = {
	.master		= &omap3xxx_l4_wkup_hwmod,
	.slave		= &omap3xxx_gpio1_hwmod,
	.addr		= omap3xxx_gpio1_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio2 */
static struct omap_hwmod_addr_space omap3xxx_gpio2_addrs[] = {
	{
		.pa_start	= 0x49050000,
		.pa_end		= 0x490501ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio2 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio2_hwmod,
	.addr		= omap3xxx_gpio2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio3 */
static struct omap_hwmod_addr_space omap3xxx_gpio3_addrs[] = {
	{
		.pa_start	= 0x49052000,
		.pa_end		= 0x490521ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio3 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio3_hwmod,
	.addr		= omap3xxx_gpio3_addrs,
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

static struct omap_mmu_dev_attr mmu_isp_dev_attr = {
	.da_start	= 0x0,
	.da_end		= 0xfffff000,
	.nr_tlb_entries = 8,
};

static struct omap_hwmod omap3xxx_mmu_isp_hwmod;
static struct omap_hwmod_irq_info omap3xxx_mmu_isp_irqs[] = {
	{ .irq = 24 + OMAP_INTC_START, },
	{ .irq = -1 }
};

static struct omap_hwmod_addr_space omap3xxx_mmu_isp_addrs[] = {
	{
		.pa_start	= 0x480bd400,
		.pa_end		= 0x480bd47f,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

/* l4_core -> mmu isp */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mmu_isp = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mmu_isp_hwmod,
	.addr		= omap3xxx_mmu_isp_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod omap3xxx_mmu_isp_hwmod = {
	.name		= "mmu_isp",
	.class		= &omap3xxx_mmu_hwmod_class,
	.mpu_irqs	= omap3xxx_mmu_isp_irqs,
	.main_clk	= "cam_ick",
	.dev_attr	= &mmu_isp_dev_attr,
	.flags		= HWMOD_NO_IDLEST,
};

#ifdef CONFIG_OMAP_IOMMU_IVA2

/* mmu iva */

static struct omap_mmu_dev_attr mmu_iva_dev_attr = {
	.da_start	= 0x11000000,
	.da_end		= 0xfffff000,
	.nr_tlb_entries = 32,
};

static struct omap_hwmod omap3xxx_mmu_iva_hwmod;
static struct omap_hwmod_irq_info omap3xxx_mmu_iva_irqs[] = {
	{ .irq = 28 + OMAP_INTC_START, },
	{ .irq = -1 }
};

static struct omap_hwmod_rst_info omap3xxx_mmu_iva_resets[] = {
	{ .name = "mmu", .rst_shift = 1, .st_shift = 9 },
};

static struct omap_hwmod_addr_space omap3xxx_mmu_iva_addrs[] = {
	{
		.pa_start	= 0x5d000000,
		.pa_end		= 0x5d00007f,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

/* l3_main -> iva mmu */
static struct omap_hwmod_ocp_if omap3xxx_l3_main__mmu_iva = {
	.master		= &omap3xxx_l3_main_hwmod,
	.slave		= &omap3xxx_mmu_iva_hwmod,
	.addr		= omap3xxx_mmu_iva_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod omap3xxx_mmu_iva_hwmod = {
	.name		= "mmu_iva",
	.class		= &omap3xxx_mmu_hwmod_class,
	.mpu_irqs	= omap3xxx_mmu_iva_irqs,
	.rst_lines	= omap3xxx_mmu_iva_resets,
	.rst_lines_cnt	= ARRAY_SIZE(omap3xxx_mmu_iva_resets),
	.main_clk	= "iva2_ck",
	.prcm = {
		.omap2 = {
			.module_offs = OMAP3430_IVA2_MOD,
		},
	},
	.dev_attr	= &mmu_iva_dev_attr,
	.flags		= HWMOD_NO_IDLEST,
};

#endif

/* l4_per -> gpio4 */
static struct omap_hwmod_addr_space omap3xxx_gpio4_addrs[] = {
	{
		.pa_start	= 0x49054000,
		.pa_end		= 0x490541ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio4 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio4_hwmod,
	.addr		= omap3xxx_gpio4_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio5 */
static struct omap_hwmod_addr_space omap3xxx_gpio5_addrs[] = {
	{
		.pa_start	= 0x49056000,
		.pa_end		= 0x490561ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio5 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio5_hwmod,
	.addr		= omap3xxx_gpio5_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio6 */
static struct omap_hwmod_addr_space omap3xxx_gpio6_addrs[] = {
	{
		.pa_start	= 0x49058000,
		.pa_end		= 0x490581ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio6 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio6_hwmod,
	.addr		= omap3xxx_gpio6_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dma_system -> L3 */
static struct omap_hwmod_ocp_if omap3xxx_dma_system__l3 = {
	.master		= &omap3xxx_dma_system_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "core_l3_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_dma_system_addrs[] = {
	{
		.pa_start	= 0x48056000,
		.pa_end		= 0x48056fff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_cfg -> dma_system */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dma_system = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dma_system_hwmod,
	.clk		= "core_l4_ick",
	.addr		= omap3xxx_dma_system_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp1_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x48074000,
		.pa_end		= 0x480740ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> mcbsp1 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mcbsp1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mcbsp1_hwmod,
	.clk		= "mcbsp1_ick",
	.addr		= omap3xxx_mcbsp1_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp2_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x49022000,
		.pa_end		= 0x490220ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> mcbsp2 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp2 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp2_hwmod,
	.clk		= "mcbsp2_ick",
	.addr		= omap3xxx_mcbsp2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp3_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x49024000,
		.pa_end		= 0x490240ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> mcbsp3 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp3 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp3_hwmod,
	.clk		= "mcbsp3_ick",
	.addr		= omap3xxx_mcbsp3_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp4_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x49026000,
		.pa_end		= 0x490260ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> mcbsp4 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp4 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp4_hwmod,
	.clk		= "mcbsp4_ick",
	.addr		= omap3xxx_mcbsp4_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp5_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x48096000,
		.pa_end		= 0x480960ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> mcbsp5 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mcbsp5 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mcbsp5_hwmod,
	.clk		= "mcbsp5_ick",
	.addr		= omap3xxx_mcbsp5_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp2_sidetone_addrs[] = {
	{
		.name		= "sidetone",
		.pa_start	= 0x49028000,
		.pa_end		= 0x490280ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> mcbsp2_sidetone */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp2_sidetone = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp2_sidetone_hwmod,
	.clk		= "mcbsp2_ick",
	.addr		= omap3xxx_mcbsp2_sidetone_addrs,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp3_sidetone_addrs[] = {
	{
		.name		= "sidetone",
		.pa_start	= 0x4902A000,
		.pa_end		= 0x4902A0ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> mcbsp3_sidetone */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp3_sidetone = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp3_sidetone_hwmod,
	.clk		= "mcbsp3_ick",
	.addr		= omap3xxx_mcbsp3_sidetone_addrs,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_addr_space omap3xxx_mailbox_addrs[] = {
	{
		.pa_start	= 0x48094000,
		.pa_end		= 0x480941ff,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

/* l4_core -> mailbox */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mailbox = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mailbox_hwmod,
	.addr		= omap3xxx_mailbox_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi1 interface */
static struct omap_hwmod_ocp_if omap34xx_l4_core__mcspi1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_mcspi1,
	.clk		= "mcspi1_ick",
	.addr		= omap2_mcspi1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi2 interface */
static struct omap_hwmod_ocp_if omap34xx_l4_core__mcspi2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_mcspi2,
	.clk		= "mcspi2_ick",
	.addr		= omap2_mcspi2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi3 interface */
static struct omap_hwmod_ocp_if omap34xx_l4_core__mcspi3 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_mcspi3,
	.clk		= "mcspi3_ick",
	.addr		= omap2430_mcspi3_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi4 interface */
static struct omap_hwmod_addr_space omap34xx_mcspi4_addr_space[] = {
	{
		.pa_start	= 0x480ba000,
		.pa_end		= 0x480ba0ff,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap34xx_l4_core__mcspi4 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_mcspi4,
	.clk		= "mcspi4_ick",
	.addr		= omap34xx_mcspi4_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if omap3xxx_usb_host_hs__l3_main_2 = {
	.master		= &omap3xxx_usb_host_hs_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "core_l3_ick",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_addr_space omap3xxx_usb_host_hs_addrs[] = {
	{
		.name		= "uhh",
		.pa_start	= 0x48064000,
		.pa_end		= 0x480643ff,
		.flags		= ADDR_TYPE_RT
	},
	{
		.name		= "ohci",
		.pa_start	= 0x48064400,
		.pa_end		= 0x480647ff,
	},
	{
		.name		= "ehci",
		.pa_start	= 0x48064800,
		.pa_end		= 0x48064cff,
	},
	{}
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__usb_host_hs = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_usb_host_hs_hwmod,
	.clk		= "usbhost_ick",
	.addr		= omap3xxx_usb_host_hs_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_addr_space omap3xxx_usb_tll_hs_addrs[] = {
	{
		.name		= "tll",
		.pa_start	= 0x48062000,
		.pa_end		= 0x48062fff,
		.flags		= ADDR_TYPE_RT
	},
	{}
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__usb_tll_hs = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_usb_tll_hs_hwmod,
	.clk		= "usbtll_ick",
	.addr		= omap3xxx_usb_tll_hs_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> hdq1w interface */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__hdq1w = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_hdq1w_hwmod,
	.clk		= "hdq_ick",
	.addr		= omap2_hdq1w_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4 | OCPIF_SWSUP_IDLE,
};

/* l4_wkup -> 32ksync_counter */
static struct omap_hwmod_addr_space omap3xxx_counter_32k_addrs[] = {
	{
		.pa_start	= 0x48320000,
		.pa_end		= 0x4832001f,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_addr_space omap3xxx_gpmc_addrs[] = {
	{
		.pa_start	= 0x6e000000,
		.pa_end		= 0x6e000fff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_wkup__counter_32k = {
	.master		= &omap3xxx_l4_wkup_hwmod,
	.slave		= &omap3xxx_counter_32k_hwmod,
	.clk		= "omap_32ksync_ick",
	.addr		= omap3xxx_counter_32k_addrs,
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

static struct omap_hwmod_addr_space am35xx_mdio_addrs[] = {
	{
		.pa_start	= AM35XX_IPSS_MDIO_BASE,
		.pa_end		= AM35XX_IPSS_MDIO_BASE + SZ_4K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
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
	.addr		= am35xx_mdio_addrs,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_irq_info am35xx_emac_mpu_irqs[] = {
	{ .name = "rxthresh",	.irq = 67 + OMAP_INTC_START, },
	{ .name = "rx_pulse",	.irq = 68 + OMAP_INTC_START, },
	{ .name = "tx_pulse",	.irq = 69 + OMAP_INTC_START },
	{ .name = "misc_pulse",	.irq = 70 + OMAP_INTC_START },
	{ .irq = -1 },
};

static struct omap_hwmod_class am35xx_emac_class = {
	.name = "davinci_emac",
};

static struct omap_hwmod am35xx_emac_hwmod = {
	.name		= "davinci_emac",
	.mpu_irqs	= am35xx_emac_mpu_irqs,
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

static struct omap_hwmod_addr_space am35xx_emac_addrs[] = {
	{
		.pa_start	= AM35XX_IPSS_EMAC_BASE,
		.pa_end		= AM35XX_IPSS_EMAC_BASE + 0x30000 - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
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
	.addr		= am35xx_emac_addrs,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if omap3xxx_l3_main__gpmc = {
	.master		= &omap3xxx_l3_main_hwmod,
	.slave		= &omap3xxx_gpmc_hwmod,
	.clk		= "core_l3_ick",
	.addr		= omap3xxx_gpmc_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> SHAM2 (SHA1/MD5) (similar to omap24xx) */
static struct omap_hwmod_sysc_fields omap3_sham_sysc_fields = {
	.sidle_shift	= 4,
	.srst_shift	= 1,
	.autoidle_shift	= 0,
};

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

static struct omap_hwmod_irq_info omap3_sham_mpu_irqs[] = {
	{ .irq = 49 + OMAP_INTC_START, },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap3_sham_sdma_reqs[] = {
	{ .name = "rx", .dma_req = OMAP34XX_DMA_SHA1MD5_RX, },
	{ .dma_req = -1 }
};

static struct omap_hwmod omap3xxx_sham_hwmod = {
	.name		= "sham",
	.mpu_irqs	= omap3_sham_mpu_irqs,
	.sdma_reqs	= omap3_sham_sdma_reqs,
	.main_clk	= "sha12_ick",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_SHA12_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_SHA12_SHIFT,
		},
	},
	.class		= &omap3xxx_sham_class,
};

static struct omap_hwmod_addr_space omap3xxx_sham_addrs[] = {
	{
		.pa_start	= 0x480c3000,
		.pa_end		= 0x480c3000 + 0x64 - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__sham = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_sham_hwmod,
	.clk		= "sha12_ick",
	.addr		= omap3xxx_sham_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> AES */
static struct omap_hwmod_sysc_fields omap3xxx_aes_sysc_fields = {
	.sidle_shift	= 6,
	.srst_shift	= 1,
	.autoidle_shift	= 0,
};

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

static struct omap_hwmod_dma_info omap3_aes_sdma_reqs[] = {
	{ .name = "tx", .dma_req = OMAP34XX_DMA_AES2_TX, },
	{ .name = "rx", .dma_req = OMAP34XX_DMA_AES2_RX, },
	{ .dma_req = -1 }
};

static struct omap_hwmod omap3xxx_aes_hwmod = {
	.name		= "aes",
	.sdma_reqs	= omap3_aes_sdma_reqs,
	.main_clk	= "aes2_ick",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_AES2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_AES2_SHIFT,
		},
	},
	.class		= &omap3xxx_aes_class,
};

static struct omap_hwmod_addr_space omap3xxx_aes_addrs[] = {
	{
		.pa_start	= 0x480c5000,
		.pa_end		= 0x480c5000 + 0x50 - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__aes = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_aes_hwmod,
	.clk		= "aes2_ick",
	.addr		= omap3xxx_aes_addrs,
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
	&omap3xxx_l4_core__sham,
	&omap3xxx_l4_core__aes,
	NULL
};

static struct omap_hwmod_ocp_if *omap36xx_gp_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_sec__timer12,
	&omap3xxx_l4_core__sham,
	&omap3xxx_l4_core__aes,
	NULL
};

static struct omap_hwmod_ocp_if *am35xx_gp_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_sec__timer12,
	/*
	 * Apparently the SHA/MD5 and AES accelerator IP blocks are
	 * only present on some AM35xx chips, and no one knows which
	 * ones.  See
	 * http://www.spinics.net/lists/arm-kernel/msg215466.html So
	 * if you need these IP blocks on an AM35xx, try uncommenting
	 * the following lines.
	 */
	/* &omap3xxx_l4_core__sham, */
	/* &omap3xxx_l4_core__aes, */
	NULL
};

/* 3430ES1-only hwmod links */
static struct omap_hwmod_ocp_if *omap3430es1_hwmod_ocp_ifs[] __initdata = {
	&omap3430es1_dss__l3,
	&omap3430es1_l4_core__dss,
	NULL
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
	NULL
};

/* <= 3430ES3-only hwmod links */
static struct omap_hwmod_ocp_if *omap3430_pre_es3_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_core__pre_es3_mmc1,
	&omap3xxx_l4_core__pre_es3_mmc2,
	NULL
};

/* 3430ES3+-only hwmod links */
static struct omap_hwmod_ocp_if *omap3430_es3plus_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_core__es3plus_mmc1,
	&omap3xxx_l4_core__es3plus_mmc2,
	NULL
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
#ifdef CONFIG_OMAP_IOMMU_IVA2
	&omap3xxx_l3_main__mmu_iva,
#endif
	NULL
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
#ifdef CONFIG_OMAP_IOMMU_IVA2
	&omap3xxx_l3_main__mmu_iva,
#endif
	NULL
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
	NULL
};

static struct omap_hwmod_ocp_if *omap3xxx_dss_hwmod_ocp_ifs[] __initdata = {
	&omap3xxx_l4_core__dss_dispc,
	&omap3xxx_l4_core__dss_dsi1,
	&omap3xxx_l4_core__dss_rfbi,
	&omap3xxx_l4_core__dss_venc,
	NULL
};

int __init omap3xxx_hwmod_init(void)
{
	int r;
	struct omap_hwmod_ocp_if **h = NULL, **h_gp = NULL;
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
	} else if (rev == AM35XX_REV_ES1_0 || rev == AM35XX_REV_ES1_1) {
		h = am35xx_hwmod_ocp_ifs;
		h_gp = am35xx_gp_hwmod_ocp_ifs;
	} else if (rev == OMAP3630_REV_ES1_0 || rev == OMAP3630_REV_ES1_1 ||
		   rev == OMAP3630_REV_ES1_2) {
		h = omap36xx_hwmod_ocp_ifs;
		h_gp = omap36xx_gp_hwmod_ocp_ifs;
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
}
