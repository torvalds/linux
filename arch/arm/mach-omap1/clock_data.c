// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-omap1/clock_data.c
 *
 *  Copyright (C) 2004 - 2005, 2009-2010 Nokia Corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *  Based on clocks.h by Tony Lindgren, Gordon McNutt and RidgeRun, Inc
 *
 * To do:
 * - Clocks that are only available on some chips should be marked with the
 *   chips that they are present on.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/soc/ti/omap1-io.h>

#include <asm/mach-types.h>  /* for machine_is_* */

#include "soc.h"
#include "hardware.h"
#include "usb.h"   /* for OTG_BASE */
#include "iomap.h"
#include "clock.h"
#include "sram.h"

/* Some ARM_IDLECT1 bit shifts - used in struct arm_idlect1_clk */
#define IDL_CLKOUT_ARM_SHIFT			12
#define IDLTIM_ARM_SHIFT			9
#define IDLAPI_ARM_SHIFT			8
#define IDLIF_ARM_SHIFT				6
#define IDLLB_ARM_SHIFT				4	/* undocumented? */
#define OMAP1510_IDLLCD_ARM_SHIFT		3	/* undocumented? */
#define IDLPER_ARM_SHIFT			2
#define IDLXORP_ARM_SHIFT			1
#define IDLWDT_ARM_SHIFT			0

/* Some MOD_CONF_CTRL_0 bit shifts - used in struct clk.enable_bit */
#define CONF_MOD_UART3_CLK_MODE_R		31
#define CONF_MOD_UART2_CLK_MODE_R		30
#define CONF_MOD_UART1_CLK_MODE_R		29
#define CONF_MOD_MMC_SD_CLK_REQ_R		23
#define CONF_MOD_MCBSP3_AUXON			20

/* Some MOD_CONF_CTRL_1 bit shifts - used in struct clk.enable_bit */
#define CONF_MOD_SOSSI_CLK_EN_R			16

/* Some OTG_SYSCON_2-specific bit fields */
#define OTG_SYSCON_2_UHOST_EN_SHIFT		8

/* Some SOFT_REQ_REG bit fields - used in struct clk.enable_bit */
#define SOFT_MMC2_DPLL_REQ_SHIFT	13
#define SOFT_MMC_DPLL_REQ_SHIFT		12
#define SOFT_UART3_DPLL_REQ_SHIFT	11
#define SOFT_UART2_DPLL_REQ_SHIFT	10
#define SOFT_UART1_DPLL_REQ_SHIFT	9
#define SOFT_USB_OTG_DPLL_REQ_SHIFT	8
#define SOFT_CAM_DPLL_REQ_SHIFT		7
#define SOFT_COM_MCKO_REQ_SHIFT		6
#define SOFT_PERIPH_REQ_SHIFT		5	/* sys_ck gate for UART2 ? */
#define USB_REQ_EN_SHIFT		4
#define SOFT_USB_REQ_SHIFT		3	/* sys_ck gate for USB host? */
#define SOFT_SDW_REQ_SHIFT		2	/* sys_ck gate for Bluetooth? */
#define SOFT_COM_REQ_SHIFT		1	/* sys_ck gate for com proc? */
#define SOFT_DPLL_REQ_SHIFT		0

/*
 * Omap1 clocks
 */

static struct omap1_clk ck_ref = {
	.hw.init	= CLK_HW_INIT_NO_PARENT("ck_ref", &omap1_clk_rate_ops, 0),
	.rate		= 12000000,
};

static struct omap1_clk ck_dpll1 = {
	.hw.init	= CLK_HW_INIT("ck_dpll1", "ck_ref", &omap1_clk_rate_ops,
				      /*
				       * force recursive refresh of rates of the clock
				       * and its children when clk_get_rate() is called
				       */
				      CLK_GET_RATE_NOCACHE),
};

/*
 * FIXME: This clock seems to be necessary but no-one has asked for its
 * activation.  [ FIX: SoSSI, SSR ]
 */
static struct arm_idlect1_clk ck_dpll1out = {
	.clk = {
		.hw.init	= CLK_HW_INIT("ck_dpll1out", "ck_dpll1", &omap1_clk_gate_ops, 0),
		.ops		= &clkops_generic,
		.flags		= CLOCK_IDLE_CONTROL | ENABLE_REG_32BIT,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_CKOUT_ARM,
	},
	.idlect_shift	= IDL_CLKOUT_ARM_SHIFT,
};

static struct omap1_clk sossi_ck = {
	.hw.init	= CLK_HW_INIT("ck_sossi", "ck_dpll1out", &omap1_clk_full_ops, 0),
	.ops		= &clkops_generic,
	.flags		= CLOCK_NO_IDLE_PARENT | ENABLE_REG_32BIT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_1),
	.enable_bit	= CONF_MOD_SOSSI_CLK_EN_R,
	.recalc		= &omap1_sossi_recalc,
	.round_rate	= &omap1_round_sossi_rate,
	.set_rate	= &omap1_set_sossi_rate,
};

static struct omap1_clk arm_ck = {
	.hw.init	= CLK_HW_INIT("arm_ck", "ck_dpll1", &omap1_clk_rate_ops, 0),
	.rate_offset	= CKCTL_ARMDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.round_rate	= omap1_clk_round_rate_ckctl_arm,
	.set_rate	= omap1_clk_set_rate_ckctl_arm,
};

static struct arm_idlect1_clk armper_ck = {
	.clk = {
		.hw.init	= CLK_HW_INIT("armper_ck", "ck_dpll1", &omap1_clk_full_ops,
					      CLK_IS_CRITICAL),
		.ops		= &clkops_generic,
		.flags		= CLOCK_IDLE_CONTROL,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_PERCK,
		.rate_offset	= CKCTL_PERDIV_OFFSET,
		.recalc		= &omap1_ckctl_recalc,
		.round_rate	= omap1_clk_round_rate_ckctl_arm,
		.set_rate	= omap1_clk_set_rate_ckctl_arm,
	},
	.idlect_shift	= IDLPER_ARM_SHIFT,
};

/*
 * FIXME: This clock seems to be necessary but no-one has asked for its
 * activation.  [ GPIO code for 1510 ]
 */
static struct omap1_clk arm_gpio_ck = {
	.hw.init	= CLK_HW_INIT("ick", "ck_dpll1", &omap1_clk_gate_ops, CLK_IS_CRITICAL),
	.ops		= &clkops_generic,
	.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
	.enable_bit	= EN_GPIOCK,
};

static struct arm_idlect1_clk armxor_ck = {
	.clk = {
		.hw.init	= CLK_HW_INIT("armxor_ck", "ck_ref", &omap1_clk_gate_ops,
					      CLK_IS_CRITICAL),
		.ops		= &clkops_generic,
		.flags		= CLOCK_IDLE_CONTROL,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_XORPCK,
	},
	.idlect_shift	= IDLXORP_ARM_SHIFT,
};

static struct arm_idlect1_clk armtim_ck = {
	.clk = {
		.hw.init	= CLK_HW_INIT("armtim_ck", "ck_ref", &omap1_clk_gate_ops,
					      CLK_IS_CRITICAL),
		.ops		= &clkops_generic,
		.flags		= CLOCK_IDLE_CONTROL,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_TIMCK,
	},
	.idlect_shift	= IDLTIM_ARM_SHIFT,
};

static struct arm_idlect1_clk armwdt_ck = {
	.clk = {
		.hw.init	= CLK_HW_INIT("armwdt_ck", "ck_ref", &omap1_clk_full_ops, 0),
		.ops		= &clkops_generic,
		.flags		= CLOCK_IDLE_CONTROL,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_WDTCK,
		.fixed_div	= 14,
		.recalc		= &omap_fixed_divisor_recalc,
	},
	.idlect_shift	= IDLWDT_ARM_SHIFT,
};

static struct omap1_clk arminth_ck16xx = {
	.hw.init	= CLK_HW_INIT("arminth_ck", "arm_ck", &omap1_clk_null_ops, 0),
	/* Note: On 16xx the frequency can be divided by 2 by programming
	 * ARM_CKCTL:ARM_INTHCK_SEL(14) to 1
	 *
	 * 1510 version is in TC clocks.
	 */
};

static struct omap1_clk dsp_ck = {
	.hw.init	= CLK_HW_INIT("dsp_ck", "ck_dpll1", &omap1_clk_full_ops, 0),
	.ops		= &clkops_generic,
	.enable_reg	= OMAP1_IO_ADDRESS(ARM_CKCTL),
	.enable_bit	= EN_DSPCK,
	.rate_offset	= CKCTL_DSPDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.round_rate	= omap1_clk_round_rate_ckctl_arm,
	.set_rate	= omap1_clk_set_rate_ckctl_arm,
};

static struct omap1_clk dspmmu_ck = {
	.hw.init	= CLK_HW_INIT("dspmmu_ck", "ck_dpll1", &omap1_clk_rate_ops, 0),
	.rate_offset	= CKCTL_DSPMMUDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.round_rate	= omap1_clk_round_rate_ckctl_arm,
	.set_rate	= omap1_clk_set_rate_ckctl_arm,
};

static struct omap1_clk dspper_ck = {
	.hw.init	= CLK_HW_INIT("dspper_ck", "ck_dpll1", &omap1_clk_full_ops, 0),
	.ops		= &clkops_dspck,
	.enable_reg	= DSP_IDLECT2,
	.enable_bit	= EN_PERCK,
	.rate_offset	= CKCTL_PERDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc_dsp_domain,
	.round_rate	= omap1_clk_round_rate_ckctl_arm,
	.set_rate	= &omap1_clk_set_rate_dsp_domain,
};

static struct omap1_clk dspxor_ck = {
	.hw.init	= CLK_HW_INIT("dspxor_ck", "ck_ref", &omap1_clk_gate_ops, 0),
	.ops		= &clkops_dspck,
	.enable_reg	= DSP_IDLECT2,
	.enable_bit	= EN_XORPCK,
};

static struct omap1_clk dsptim_ck = {
	.hw.init	= CLK_HW_INIT("dsptim_ck", "ck_ref", &omap1_clk_gate_ops, 0),
	.ops		= &clkops_dspck,
	.enable_reg	= DSP_IDLECT2,
	.enable_bit	= EN_DSPTIMCK,
};

static struct arm_idlect1_clk tc_ck = {
	.clk = {
		.hw.init	= CLK_HW_INIT("tc_ck", "ck_dpll1", &omap1_clk_rate_ops, 0),
		.flags		= CLOCK_IDLE_CONTROL,
		.rate_offset	= CKCTL_TCDIV_OFFSET,
		.recalc		= &omap1_ckctl_recalc,
		.round_rate	= omap1_clk_round_rate_ckctl_arm,
		.set_rate	= omap1_clk_set_rate_ckctl_arm,
	},
	.idlect_shift	= IDLIF_ARM_SHIFT,
};

static struct omap1_clk arminth_ck1510 = {
	.hw.init	= CLK_HW_INIT("arminth_ck", "tc_ck", &omap1_clk_null_ops, 0),
	/* Note: On 1510 the frequency follows TC_CK
	 *
	 * 16xx version is in MPU clocks.
	 */
};

static struct omap1_clk tipb_ck = {
	/* No-idle controlled by "tc_ck" */
	.hw.init	= CLK_HW_INIT("tipb_ck", "tc_ck", &omap1_clk_null_ops, 0),
};

static struct omap1_clk l3_ocpi_ck = {
	/* No-idle controlled by "tc_ck" */
	.hw.init	= CLK_HW_INIT("l3_ocpi_ck", "tc_ck", &omap1_clk_gate_ops, 0),
	.ops		= &clkops_generic,
	.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT3),
	.enable_bit	= EN_OCPI_CK,
};

static struct omap1_clk tc1_ck = {
	.hw.init	= CLK_HW_INIT("tc1_ck", "tc_ck", &omap1_clk_gate_ops, 0),
	.ops		= &clkops_generic,
	.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT3),
	.enable_bit	= EN_TC1_CK,
};

/*
 * FIXME: This clock seems to be necessary but no-one has asked for its
 * activation.  [ pm.c (SRAM), CCP, Camera ]
 */

static struct omap1_clk tc2_ck = {
	.hw.init	= CLK_HW_INIT("tc2_ck", "tc_ck", &omap1_clk_gate_ops, CLK_IS_CRITICAL),
	.ops		= &clkops_generic,
	.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT3),
	.enable_bit	= EN_TC2_CK,
};

static struct omap1_clk dma_ck = {
	/* No-idle controlled by "tc_ck" */
	.hw.init	= CLK_HW_INIT("dma_ck", "tc_ck", &omap1_clk_null_ops, 0),
};

static struct omap1_clk dma_lcdfree_ck = {
	.hw.init	= CLK_HW_INIT("dma_lcdfree_ck", "tc_ck", &omap1_clk_null_ops, 0),
};

static struct arm_idlect1_clk api_ck = {
	.clk = {
		.hw.init	= CLK_HW_INIT("api_ck", "tc_ck", &omap1_clk_gate_ops, 0),
		.ops		= &clkops_generic,
		.flags		= CLOCK_IDLE_CONTROL,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_APICK,
	},
	.idlect_shift	= IDLAPI_ARM_SHIFT,
};

static struct arm_idlect1_clk lb_ck = {
	.clk = {
		.hw.init	= CLK_HW_INIT("lb_ck", "tc_ck", &omap1_clk_gate_ops, 0),
		.ops		= &clkops_generic,
		.flags		= CLOCK_IDLE_CONTROL,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_LBCK,
	},
	.idlect_shift	= IDLLB_ARM_SHIFT,
};

static struct omap1_clk rhea1_ck = {
	.hw.init	= CLK_HW_INIT("rhea1_ck", "tc_ck", &omap1_clk_null_ops, 0),
};

static struct omap1_clk rhea2_ck = {
	.hw.init	= CLK_HW_INIT("rhea2_ck", "tc_ck", &omap1_clk_null_ops, 0),
};

static struct omap1_clk lcd_ck_16xx = {
	.hw.init	= CLK_HW_INIT("lcd_ck", "ck_dpll1", &omap1_clk_full_ops, 0),
	.ops		= &clkops_generic,
	.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
	.enable_bit	= EN_LCDCK,
	.rate_offset	= CKCTL_LCDDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.round_rate	= omap1_clk_round_rate_ckctl_arm,
	.set_rate	= omap1_clk_set_rate_ckctl_arm,
};

static struct arm_idlect1_clk lcd_ck_1510 = {
	.clk = {
		.hw.init	= CLK_HW_INIT("lcd_ck", "ck_dpll1", &omap1_clk_full_ops, 0),
		.ops		= &clkops_generic,
		.flags		= CLOCK_IDLE_CONTROL,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_LCDCK,
		.rate_offset	= CKCTL_LCDDIV_OFFSET,
		.recalc		= &omap1_ckctl_recalc,
		.round_rate	= omap1_clk_round_rate_ckctl_arm,
		.set_rate	= omap1_clk_set_rate_ckctl_arm,
	},
	.idlect_shift	= OMAP1510_IDLLCD_ARM_SHIFT,
};


/*
 * XXX The enable_bit here is misused - it simply switches between 12MHz
 * and 48MHz.  Reimplement with clk_mux.
 *
 * XXX does this need SYSC register handling?
 */
static struct omap1_clk uart1_1510 = {
	/* Direct from ULPD, no real parent */
	.hw.init	= CLK_HW_INIT("uart1_ck", "armper_ck", &omap1_clk_full_ops, 0),
	.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
	.enable_bit	= CONF_MOD_UART1_CLK_MODE_R,
	.round_rate	= &omap1_round_uart_rate,
	.set_rate	= &omap1_set_uart_rate,
	.recalc		= &omap1_uart_recalc,
};

/*
 * XXX The enable_bit here is misused - it simply switches between 12MHz
 * and 48MHz.  Reimplement with clk_mux.
 *
 * XXX SYSC register handling does not belong in the clock framework
 */
static struct uart_clk uart1_16xx = {
	.clk	= {
		.ops		= &clkops_uart_16xx,
		/* Direct from ULPD, no real parent */
		.hw.init	= CLK_HW_INIT("uart1_ck", "armper_ck", &omap1_clk_full_ops, 0),
		.rate		= 48000000,
		.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
		.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
		.enable_bit	= CONF_MOD_UART1_CLK_MODE_R,
	},
	.sysc_addr	= 0xfffb0054,
};

/*
 * XXX The enable_bit here is misused - it simply switches between 12MHz
 * and 48MHz.  Reimplement with clk_mux.
 *
 * XXX does this need SYSC register handling?
 */
static struct omap1_clk uart2_ck = {
	/* Direct from ULPD, no real parent */
	.hw.init	= CLK_HW_INIT("uart2_ck", "armper_ck", &omap1_clk_full_ops, 0),
	.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
	.enable_bit	= CONF_MOD_UART2_CLK_MODE_R,
	.round_rate	= &omap1_round_uart_rate,
	.set_rate	= &omap1_set_uart_rate,
	.recalc		= &omap1_uart_recalc,
};

/*
 * XXX The enable_bit here is misused - it simply switches between 12MHz
 * and 48MHz.  Reimplement with clk_mux.
 *
 * XXX does this need SYSC register handling?
 */
static struct omap1_clk uart3_1510 = {
	/* Direct from ULPD, no real parent */
	.hw.init	= CLK_HW_INIT("uart3_ck", "armper_ck", &omap1_clk_full_ops, 0),
	.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
	.enable_bit	= CONF_MOD_UART3_CLK_MODE_R,
	.round_rate	= &omap1_round_uart_rate,
	.set_rate	= &omap1_set_uart_rate,
	.recalc		= &omap1_uart_recalc,
};

/*
 * XXX The enable_bit here is misused - it simply switches between 12MHz
 * and 48MHz.  Reimplement with clk_mux.
 *
 * XXX SYSC register handling does not belong in the clock framework
 */
static struct uart_clk uart3_16xx = {
	.clk	= {
		.ops		= &clkops_uart_16xx,
		/* Direct from ULPD, no real parent */
		.hw.init	= CLK_HW_INIT("uart3_ck", "armper_ck", &omap1_clk_full_ops, 0),
		.rate		= 48000000,
		.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
		.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
		.enable_bit	= CONF_MOD_UART3_CLK_MODE_R,
	},
	.sysc_addr	= 0xfffb9854,
};

static struct omap1_clk usb_clko = {	/* 6 MHz output on W4_USB_CLKO */
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent */
	.hw.init	= CLK_HW_INIT_NO_PARENT("usb_clko", &omap1_clk_full_ops, 0),
	.rate		= 6000000,
	.flags		= ENABLE_REG_32BIT,
	.enable_reg	= OMAP1_IO_ADDRESS(ULPD_CLOCK_CTRL),
	.enable_bit	= USB_MCLK_EN_BIT,
};

static struct omap1_clk usb_hhc_ck1510 = {
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent */
	.hw.init	= CLK_HW_INIT_NO_PARENT("usb_hhc_ck", &omap1_clk_full_ops, 0),
	.rate		= 48000000, /* Actually 2 clocks, 12MHz and 48MHz */
	.flags		= ENABLE_REG_32BIT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
	.enable_bit	= USB_HOST_HHC_UHOST_EN,
};

static struct omap1_clk usb_hhc_ck16xx = {
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent */
	.hw.init	= CLK_HW_INIT_NO_PARENT("usb_hhc_ck", &omap1_clk_full_ops, 0),
	.rate		= 48000000,
	/* OTG_SYSCON_2.OTG_PADEN == 0 (not 1510-compatible) */
	.flags		= ENABLE_REG_32BIT,
	.enable_reg	= OMAP1_IO_ADDRESS(OTG_BASE + 0x08), /* OTG_SYSCON_2 */
	.enable_bit	= OTG_SYSCON_2_UHOST_EN_SHIFT
};

static struct omap1_clk usb_dc_ck = {
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent */
	.hw.init	= CLK_HW_INIT_NO_PARENT("usb_dc_ck", &omap1_clk_full_ops, 0),
	.rate		= 48000000,
	.enable_reg	= OMAP1_IO_ADDRESS(SOFT_REQ_REG),
	.enable_bit	= SOFT_USB_OTG_DPLL_REQ_SHIFT,
};

static struct omap1_clk uart1_7xx = {
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent */
	.hw.init	= CLK_HW_INIT_NO_PARENT("uart1_ck", &omap1_clk_full_ops, 0),
	.rate		= 12000000,
	.enable_reg	= OMAP1_IO_ADDRESS(SOFT_REQ_REG),
	.enable_bit	= 9,
};

static struct omap1_clk uart2_7xx = {
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent */
	.hw.init	= CLK_HW_INIT_NO_PARENT("uart2_ck", &omap1_clk_full_ops, 0),
	.rate		= 12000000,
	.enable_reg	= OMAP1_IO_ADDRESS(SOFT_REQ_REG),
	.enable_bit	= 11,
};

static struct omap1_clk mclk_1510 = {
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.hw.init	= CLK_HW_INIT_NO_PARENT("mclk", &omap1_clk_full_ops, 0),
	.rate		= 12000000,
	.enable_reg	= OMAP1_IO_ADDRESS(SOFT_REQ_REG),
	.enable_bit	= SOFT_COM_MCKO_REQ_SHIFT,
};

static struct omap1_clk mclk_16xx = {
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.hw.init	= CLK_HW_INIT_NO_PARENT("mclk", &omap1_clk_full_ops, 0),
	.enable_reg	= OMAP1_IO_ADDRESS(COM_CLK_DIV_CTRL_SEL),
	.enable_bit	= COM_ULPD_PLL_CLK_REQ,
	.set_rate	= &omap1_set_ext_clk_rate,
	.round_rate	= &omap1_round_ext_clk_rate,
	.init		= &omap1_init_ext_clk,
};

static struct omap1_clk bclk_1510 = {
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.hw.init	= CLK_HW_INIT_NO_PARENT("bclk", &omap1_clk_rate_ops, 0),
	.rate		= 12000000,
};

static struct omap1_clk bclk_16xx = {
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.hw.init	= CLK_HW_INIT_NO_PARENT("bclk", &omap1_clk_full_ops, 0),
	.enable_reg	= OMAP1_IO_ADDRESS(SWD_CLK_DIV_CTRL_SEL),
	.enable_bit	= SWD_ULPD_PLL_CLK_REQ,
	.set_rate	= &omap1_set_ext_clk_rate,
	.round_rate	= &omap1_round_ext_clk_rate,
	.init		= &omap1_init_ext_clk,
};

static struct omap1_clk mmc1_ck = {
	.ops		= &clkops_generic,
	/* Functional clock is direct from ULPD, interface clock is ARMPER */
	.hw.init	= CLK_HW_INIT("mmc1_ck", "armper_ck", &omap1_clk_full_ops, 0),
	.rate		= 48000000,
	.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
	.enable_bit	= CONF_MOD_MMC_SD_CLK_REQ_R,
};

/*
 * XXX MOD_CONF_CTRL_0 bit 20 is defined in the 1510 TRM as
 * CONF_MOD_MCBSP3_AUXON ??
 */
static struct omap1_clk mmc2_ck = {
	.ops		= &clkops_generic,
	/* Functional clock is direct from ULPD, interface clock is ARMPER */
	.hw.init	= CLK_HW_INIT("mmc2_ck", "armper_ck", &omap1_clk_full_ops, 0),
	.rate		= 48000000,
	.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
	.enable_bit	= 20,
};

static struct omap1_clk mmc3_ck = {
	.ops		= &clkops_generic,
	/* Functional clock is direct from ULPD, interface clock is ARMPER */
	.hw.init	= CLK_HW_INIT("mmc3_ck", "armper_ck", &omap1_clk_full_ops, 0),
	.rate		= 48000000,
	.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= OMAP1_IO_ADDRESS(SOFT_REQ_REG),
	.enable_bit	= SOFT_MMC_DPLL_REQ_SHIFT,
};

static struct omap1_clk virtual_ck_mpu = {
	/* Is smarter alias for arm_ck */
	.hw.init	= CLK_HW_INIT("mpu", "arm_ck", &omap1_clk_rate_ops, 0),
	.recalc		= &followparent_recalc,
	.set_rate	= &omap1_select_table_rate,
	.round_rate	= &omap1_round_to_table_rate,
};

/* virtual functional clock domain for I2C. Just for making sure that ARMXOR_CK
remains active during MPU idle whenever this is enabled */
static struct omap1_clk i2c_fck = {
	.hw.init	= CLK_HW_INIT("i2c_fck", "armxor_ck", &omap1_clk_gate_ops, 0),
	.flags		= CLOCK_NO_IDLE_PARENT,
};

static struct omap1_clk i2c_ick = {
	.hw.init	= CLK_HW_INIT("i2c_ick", "armper_ck", &omap1_clk_gate_ops, 0),
	.flags		= CLOCK_NO_IDLE_PARENT,
};

/*
 * clkdev integration
 */

static struct omap_clk omap_clks[] = {
	/* non-ULPD clocks */
	CLK(NULL,	"ck_ref",	&ck_ref.hw,	CK_16XX | CK_1510 | CK_310 | CK_7XX),
	CLK(NULL,	"ck_dpll1",	&ck_dpll1.hw,	CK_16XX | CK_1510 | CK_310 | CK_7XX),
	/* CK_GEN1 clocks */
	CLK(NULL,	"ck_dpll1out",	&ck_dpll1out.clk.hw, CK_16XX),
	CLK(NULL,	"ck_sossi",	&sossi_ck.hw,	CK_16XX),
	CLK(NULL,	"arm_ck",	&arm_ck.hw,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"armper_ck",	&armper_ck.clk.hw, CK_16XX | CK_1510 | CK_310),
	CLK("omap_gpio.0", "ick",	&arm_gpio_ck.hw, CK_1510 | CK_310),
	CLK(NULL,	"armxor_ck",	&armxor_ck.clk.hw, CK_16XX | CK_1510 | CK_310 | CK_7XX),
	CLK(NULL,	"armtim_ck",	&armtim_ck.clk.hw, CK_16XX | CK_1510 | CK_310),
	CLK("omap_wdt",	"fck",		&armwdt_ck.clk.hw, CK_16XX | CK_1510 | CK_310),
	CLK("omap_wdt",	"ick",		&armper_ck.clk.hw, CK_16XX),
	CLK("omap_wdt", "ick",		&dummy_ck.hw,	CK_1510 | CK_310),
	CLK(NULL,	"arminth_ck",	&arminth_ck1510.hw, CK_1510 | CK_310),
	CLK(NULL,	"arminth_ck",	&arminth_ck16xx.hw, CK_16XX),
	/* CK_GEN2 clocks */
	CLK(NULL,	"dsp_ck",	&dsp_ck.hw,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dspmmu_ck",	&dspmmu_ck.hw,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dspper_ck",	&dspper_ck.hw,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dspxor_ck",	&dspxor_ck.hw,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dsptim_ck",	&dsptim_ck.hw,	CK_16XX | CK_1510 | CK_310),
	/* CK_GEN3 clocks */
	CLK(NULL,	"tc_ck",	&tc_ck.clk.hw,	CK_16XX | CK_1510 | CK_310 | CK_7XX),
	CLK(NULL,	"tipb_ck",	&tipb_ck.hw,	CK_1510 | CK_310),
	CLK(NULL,	"l3_ocpi_ck",	&l3_ocpi_ck.hw,	CK_16XX | CK_7XX),
	CLK(NULL,	"tc1_ck",	&tc1_ck.hw,	CK_16XX),
	CLK(NULL,	"tc2_ck",	&tc2_ck.hw,	CK_16XX),
	CLK(NULL,	"dma_ck",	&dma_ck.hw,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dma_lcdfree_ck", &dma_lcdfree_ck.hw, CK_16XX),
	CLK(NULL,	"api_ck",	&api_ck.clk.hw,	CK_16XX | CK_1510 | CK_310 | CK_7XX),
	CLK(NULL,	"lb_ck",	&lb_ck.clk.hw,	CK_1510 | CK_310),
	CLK(NULL,	"rhea1_ck",	&rhea1_ck.hw,	CK_16XX),
	CLK(NULL,	"rhea2_ck",	&rhea2_ck.hw,	CK_16XX),
	CLK(NULL,	"lcd_ck",	&lcd_ck_16xx.hw, CK_16XX | CK_7XX),
	CLK(NULL,	"lcd_ck",	&lcd_ck_1510.clk.hw, CK_1510 | CK_310),
	/* ULPD clocks */
	CLK(NULL,	"uart1_ck",	&uart1_1510.hw,	CK_1510 | CK_310),
	CLK(NULL,	"uart1_ck",	&uart1_16xx.clk.hw, CK_16XX),
	CLK(NULL,	"uart1_ck",	&uart1_7xx.hw,	CK_7XX),
	CLK(NULL,	"uart2_ck",	&uart2_ck.hw,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"uart2_ck",	&uart2_7xx.hw,	CK_7XX),
	CLK(NULL,	"uart3_ck",	&uart3_1510.hw,	CK_1510 | CK_310),
	CLK(NULL,	"uart3_ck",	&uart3_16xx.clk.hw, CK_16XX),
	CLK(NULL,	"usb_clko",	&usb_clko.hw,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"usb_hhc_ck",	&usb_hhc_ck1510.hw, CK_1510 | CK_310),
	CLK(NULL,	"usb_hhc_ck",	&usb_hhc_ck16xx.hw, CK_16XX),
	CLK(NULL,	"usb_dc_ck",	&usb_dc_ck.hw,	CK_16XX | CK_7XX),
	CLK(NULL,	"mclk",		&mclk_1510.hw,	CK_1510 | CK_310),
	CLK(NULL,	"mclk",		&mclk_16xx.hw,	CK_16XX),
	CLK(NULL,	"bclk",		&bclk_1510.hw,	CK_1510 | CK_310),
	CLK(NULL,	"bclk",		&bclk_16xx.hw,	CK_16XX),
	CLK("mmci-omap.0", "fck",	&mmc1_ck.hw,	CK_16XX | CK_1510 | CK_310),
	CLK("mmci-omap.0", "fck",	&mmc3_ck.hw,	CK_7XX),
	CLK("mmci-omap.0", "ick",	&armper_ck.clk.hw, CK_16XX | CK_1510 | CK_310 | CK_7XX),
	CLK("mmci-omap.1", "fck",	&mmc2_ck.hw,	CK_16XX),
	CLK("mmci-omap.1", "ick",	&armper_ck.clk.hw, CK_16XX),
	/* Virtual clocks */
	CLK(NULL,	"mpu",		&virtual_ck_mpu.hw, CK_16XX | CK_1510 | CK_310),
	CLK("omap_i2c.1", "fck",	&i2c_fck.hw,	CK_16XX | CK_1510 | CK_310 | CK_7XX),
	CLK("omap_i2c.1", "ick",	&i2c_ick.hw,	CK_16XX),
	CLK("omap_i2c.1", "ick",	&dummy_ck.hw,	CK_1510 | CK_310 | CK_7XX),
	CLK("omap1_spi100k.1", "fck",	&dummy_ck.hw,	CK_7XX),
	CLK("omap1_spi100k.1", "ick",	&dummy_ck.hw,	CK_7XX),
	CLK("omap1_spi100k.2", "fck",	&dummy_ck.hw,	CK_7XX),
	CLK("omap1_spi100k.2", "ick",	&dummy_ck.hw,	CK_7XX),
	CLK("omap_uwire", "fck",	&armxor_ck.clk.hw, CK_16XX | CK_1510 | CK_310),
	CLK("omap-mcbsp.1", "ick",	&dspper_ck.hw,	CK_16XX),
	CLK("omap-mcbsp.1", "ick",	&dummy_ck.hw,	CK_1510 | CK_310),
	CLK("omap-mcbsp.2", "ick",	&armper_ck.clk.hw, CK_16XX),
	CLK("omap-mcbsp.2", "ick",	&dummy_ck.hw,	CK_1510 | CK_310),
	CLK("omap-mcbsp.3", "ick",	&dspper_ck.hw,	CK_16XX),
	CLK("omap-mcbsp.3", "ick",	&dummy_ck.hw,	CK_1510 | CK_310),
	CLK("omap-mcbsp.1", "fck",	&dspxor_ck.hw,	CK_16XX | CK_1510 | CK_310),
	CLK("omap-mcbsp.2", "fck",	&armper_ck.clk.hw, CK_16XX | CK_1510 | CK_310),
	CLK("omap-mcbsp.3", "fck",	&dspxor_ck.hw,	CK_16XX | CK_1510 | CK_310),
};

/*
 * init
 */

static void __init omap1_show_rates(void)
{
	pr_notice("Clocking rate (xtal/DPLL1/MPU): %ld.%01ld/%ld.%01ld/%ld.%01ld MHz\n",
		  ck_ref.rate / 1000000, (ck_ref.rate / 100000) % 10,
		  ck_dpll1.rate / 1000000, (ck_dpll1.rate / 100000) % 10,
		  arm_ck.rate / 1000000, (arm_ck.rate / 100000) % 10);
}

u32 cpu_mask;

int __init omap1_clk_init(void)
{
	struct omap_clk *c;
	u32 reg;

#ifdef CONFIG_DEBUG_LL
	/* Make sure UART clocks are enabled early */
	if (cpu_is_omap16xx())
		omap_writel(omap_readl(MOD_CONF_CTRL_0) |
			    CONF_MOD_UART1_CLK_MODE_R |
			    CONF_MOD_UART3_CLK_MODE_R, MOD_CONF_CTRL_0);
#endif

	/* USB_REQ_EN will be disabled later if necessary (usb_dc_ck) */
	reg = omap_readw(SOFT_REQ_REG) & (1 << 4);
	omap_writew(reg, SOFT_REQ_REG);
	if (!cpu_is_omap15xx())
		omap_writew(0, SOFT_REQ_REG2);

	/* By default all idlect1 clocks are allowed to idle */
	arm_idlect1_mask = ~0;

	cpu_mask = 0;
	if (cpu_is_omap1710())
		cpu_mask |= CK_1710;
	if (cpu_is_omap16xx())
		cpu_mask |= CK_16XX;
	if (cpu_is_omap1510())
		cpu_mask |= CK_1510;
	if (cpu_is_omap310())
		cpu_mask |= CK_310;

	/* Pointers to these clocks are needed by code in clock.c */
	api_ck_p = &api_ck.clk;
	ck_dpll1_p = &ck_dpll1;
	ck_ref_p = &ck_ref;

	pr_info("Clocks: ARM_SYSST: 0x%04x DPLL_CTL: 0x%04x ARM_CKCTL: 0x%04x\n",
		omap_readw(ARM_SYSST), omap_readw(DPLL_CTL),
		omap_readw(ARM_CKCTL));

	/* We want to be in synchronous scalable mode */
	omap_writew(0x1000, ARM_SYSST);


	/*
	 * Initially use the values set by bootloader. Determine PLL rate and
	 * recalculate dependent clocks as if kernel had changed PLL or
	 * divisors. See also omap1_clk_late_init() that can reprogram dpll1
	 * after the SRAM is initialized.
	 */
	{
		unsigned pll_ctl_val = omap_readw(DPLL_CTL);

		ck_dpll1.rate = ck_ref.rate; /* Base xtal rate */
		if (pll_ctl_val & 0x10) {
			/* PLL enabled, apply multiplier and divisor */
			if (pll_ctl_val & 0xf80)
				ck_dpll1.rate *= (pll_ctl_val & 0xf80) >> 7;
			ck_dpll1.rate /= ((pll_ctl_val & 0x60) >> 5) + 1;
		} else {
			/* PLL disabled, apply bypass divisor */
			switch (pll_ctl_val & 0xc) {
			case 0:
				break;
			case 0x4:
				ck_dpll1.rate /= 2;
				break;
			default:
				ck_dpll1.rate /= 4;
				break;
			}
		}
	}

	/* Amstrad Delta wants BCLK high when inactive */
	if (machine_is_ams_delta())
		omap_writel(omap_readl(ULPD_CLOCK_CTRL) |
				(1 << SDW_MCLK_INV_BIT),
				ULPD_CLOCK_CTRL);

	/* Turn off DSP and ARM_TIMXO. Make sure ARM_INTHCK is not divided */
	omap_writew(omap_readw(ARM_CKCTL) & 0x0fff, ARM_CKCTL);

	/* Put DSP/MPUI into reset until needed */
	omap_writew(0, ARM_RSTCT1);
	omap_writew(1, ARM_RSTCT2);
	omap_writew(0x400, ARM_IDLECT1);

	/*
	 * According to OMAP5910 Erratum SYS_DMA_1, bit DMACK_REQ (bit 8)
	 * of the ARM_IDLECT2 register must be set to zero. The power-on
	 * default value of this bit is one.
	 */
	omap_writew(0x0000, ARM_IDLECT2);	/* Turn LCD clock off also */

	for (c = omap_clks; c < omap_clks + ARRAY_SIZE(omap_clks); c++) {
		if (!(c->cpu & cpu_mask))
			continue;

		if (c->lk.clk_hw->init) { /* NULL if provider already registered */
			const struct clk_init_data *init = c->lk.clk_hw->init;
			const char *name = c->lk.clk_hw->init->name;
			int err;

			err = clk_hw_register(NULL, c->lk.clk_hw);
			if (err < 0) {
				pr_err("failed to register clock \"%s\"! (%d)\n", name, err);
				/* may be tried again, restore init data */
				c->lk.clk_hw->init = init;
				continue;
			}
		}

		clk_hw_register_clkdev(c->lk.clk_hw, c->lk.con_id, c->lk.dev_id);
	}

	omap1_show_rates();

	return 0;
}

#define OMAP1_DPLL1_SANE_VALUE	60000000

void __init omap1_clk_late_init(void)
{
	unsigned long rate = ck_dpll1.rate;

	/* Find the highest supported frequency and enable it */
	if (omap1_select_table_rate(&virtual_ck_mpu, ~0, arm_ck.rate)) {
		pr_err("System frequencies not set, using default. Check your config.\n");
		/*
		 * Reprogramming the DPLL is tricky, it must be done from SRAM.
		 */
		omap_sram_reprogram_clock(0x2290, 0x0005);
		ck_dpll1.rate = OMAP1_DPLL1_SANE_VALUE;
	}
	propagate_rate(&ck_dpll1);
	omap1_show_rates();
	loops_per_jiffy = cpufreq_scale(loops_per_jiffy, rate, ck_dpll1.rate);
}
