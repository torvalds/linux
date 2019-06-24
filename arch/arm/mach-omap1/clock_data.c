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
#include <linux/cpufreq.h>
#include <linux/delay.h>

#include <asm/mach-types.h>  /* for machine_is_* */

#include "soc.h"

#include <mach/hardware.h>
#include <mach/usb.h>   /* for OTG_BASE */

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

static struct clk ck_ref = {
	.name		= "ck_ref",
	.ops		= &clkops_null,
	.rate		= 12000000,
};

static struct clk ck_dpll1 = {
	.name		= "ck_dpll1",
	.ops		= &clkops_null,
	.parent		= &ck_ref,
};

/*
 * FIXME: This clock seems to be necessary but no-one has asked for its
 * activation.  [ FIX: SoSSI, SSR ]
 */
static struct arm_idlect1_clk ck_dpll1out = {
	.clk = {
		.name		= "ck_dpll1out",
		.ops		= &clkops_generic,
		.parent		= &ck_dpll1,
		.flags		= CLOCK_IDLE_CONTROL | ENABLE_REG_32BIT |
				  ENABLE_ON_INIT,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_CKOUT_ARM,
		.recalc		= &followparent_recalc,
	},
	.idlect_shift	= IDL_CLKOUT_ARM_SHIFT,
};

static struct clk sossi_ck = {
	.name		= "ck_sossi",
	.ops		= &clkops_generic,
	.parent		= &ck_dpll1out.clk,
	.flags		= CLOCK_NO_IDLE_PARENT | ENABLE_REG_32BIT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_1),
	.enable_bit	= CONF_MOD_SOSSI_CLK_EN_R,
	.recalc		= &omap1_sossi_recalc,
	.set_rate	= &omap1_set_sossi_rate,
};

static struct clk arm_ck = {
	.name		= "arm_ck",
	.ops		= &clkops_null,
	.parent		= &ck_dpll1,
	.rate_offset	= CKCTL_ARMDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.round_rate	= omap1_clk_round_rate_ckctl_arm,
	.set_rate	= omap1_clk_set_rate_ckctl_arm,
};

static struct arm_idlect1_clk armper_ck = {
	.clk = {
		.name		= "armper_ck",
		.ops		= &clkops_generic,
		.parent		= &ck_dpll1,
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
static struct clk arm_gpio_ck = {
	.name		= "ick",
	.ops		= &clkops_generic,
	.parent		= &ck_dpll1,
	.flags		= ENABLE_ON_INIT,
	.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
	.enable_bit	= EN_GPIOCK,
	.recalc		= &followparent_recalc,
};

static struct arm_idlect1_clk armxor_ck = {
	.clk = {
		.name		= "armxor_ck",
		.ops		= &clkops_generic,
		.parent		= &ck_ref,
		.flags		= CLOCK_IDLE_CONTROL,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_XORPCK,
		.recalc		= &followparent_recalc,
	},
	.idlect_shift	= IDLXORP_ARM_SHIFT,
};

static struct arm_idlect1_clk armtim_ck = {
	.clk = {
		.name		= "armtim_ck",
		.ops		= &clkops_generic,
		.parent		= &ck_ref,
		.flags		= CLOCK_IDLE_CONTROL,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_TIMCK,
		.recalc		= &followparent_recalc,
	},
	.idlect_shift	= IDLTIM_ARM_SHIFT,
};

static struct arm_idlect1_clk armwdt_ck = {
	.clk = {
		.name		= "armwdt_ck",
		.ops		= &clkops_generic,
		.parent		= &ck_ref,
		.flags		= CLOCK_IDLE_CONTROL,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_WDTCK,
		.fixed_div	= 14,
		.recalc		= &omap_fixed_divisor_recalc,
	},
	.idlect_shift	= IDLWDT_ARM_SHIFT,
};

static struct clk arminth_ck16xx = {
	.name		= "arminth_ck",
	.ops		= &clkops_null,
	.parent		= &arm_ck,
	.recalc		= &followparent_recalc,
	/* Note: On 16xx the frequency can be divided by 2 by programming
	 * ARM_CKCTL:ARM_INTHCK_SEL(14) to 1
	 *
	 * 1510 version is in TC clocks.
	 */
};

static struct clk dsp_ck = {
	.name		= "dsp_ck",
	.ops		= &clkops_generic,
	.parent		= &ck_dpll1,
	.enable_reg	= OMAP1_IO_ADDRESS(ARM_CKCTL),
	.enable_bit	= EN_DSPCK,
	.rate_offset	= CKCTL_DSPDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.round_rate	= omap1_clk_round_rate_ckctl_arm,
	.set_rate	= omap1_clk_set_rate_ckctl_arm,
};

static struct clk dspmmu_ck = {
	.name		= "dspmmu_ck",
	.ops		= &clkops_null,
	.parent		= &ck_dpll1,
	.rate_offset	= CKCTL_DSPMMUDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.round_rate	= omap1_clk_round_rate_ckctl_arm,
	.set_rate	= omap1_clk_set_rate_ckctl_arm,
};

static struct clk dspper_ck = {
	.name		= "dspper_ck",
	.ops		= &clkops_dspck,
	.parent		= &ck_dpll1,
	.enable_reg	= DSP_IDLECT2,
	.enable_bit	= EN_PERCK,
	.rate_offset	= CKCTL_PERDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc_dsp_domain,
	.round_rate	= omap1_clk_round_rate_ckctl_arm,
	.set_rate	= &omap1_clk_set_rate_dsp_domain,
};

static struct clk dspxor_ck = {
	.name		= "dspxor_ck",
	.ops		= &clkops_dspck,
	.parent		= &ck_ref,
	.enable_reg	= DSP_IDLECT2,
	.enable_bit	= EN_XORPCK,
	.recalc		= &followparent_recalc,
};

static struct clk dsptim_ck = {
	.name		= "dsptim_ck",
	.ops		= &clkops_dspck,
	.parent		= &ck_ref,
	.enable_reg	= DSP_IDLECT2,
	.enable_bit	= EN_DSPTIMCK,
	.recalc		= &followparent_recalc,
};

static struct arm_idlect1_clk tc_ck = {
	.clk = {
		.name		= "tc_ck",
		.ops		= &clkops_null,
		.parent		= &ck_dpll1,
		.flags		= CLOCK_IDLE_CONTROL,
		.rate_offset	= CKCTL_TCDIV_OFFSET,
		.recalc		= &omap1_ckctl_recalc,
		.round_rate	= omap1_clk_round_rate_ckctl_arm,
		.set_rate	= omap1_clk_set_rate_ckctl_arm,
	},
	.idlect_shift	= IDLIF_ARM_SHIFT,
};

static struct clk arminth_ck1510 = {
	.name		= "arminth_ck",
	.ops		= &clkops_null,
	.parent		= &tc_ck.clk,
	.recalc		= &followparent_recalc,
	/* Note: On 1510 the frequency follows TC_CK
	 *
	 * 16xx version is in MPU clocks.
	 */
};

static struct clk tipb_ck = {
	/* No-idle controlled by "tc_ck" */
	.name		= "tipb_ck",
	.ops		= &clkops_null,
	.parent		= &tc_ck.clk,
	.recalc		= &followparent_recalc,
};

static struct clk l3_ocpi_ck = {
	/* No-idle controlled by "tc_ck" */
	.name		= "l3_ocpi_ck",
	.ops		= &clkops_generic,
	.parent		= &tc_ck.clk,
	.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT3),
	.enable_bit	= EN_OCPI_CK,
	.recalc		= &followparent_recalc,
};

static struct clk tc1_ck = {
	.name		= "tc1_ck",
	.ops		= &clkops_generic,
	.parent		= &tc_ck.clk,
	.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT3),
	.enable_bit	= EN_TC1_CK,
	.recalc		= &followparent_recalc,
};

/*
 * FIXME: This clock seems to be necessary but no-one has asked for its
 * activation.  [ pm.c (SRAM), CCP, Camera ]
 */
static struct clk tc2_ck = {
	.name		= "tc2_ck",
	.ops		= &clkops_generic,
	.parent		= &tc_ck.clk,
	.flags		= ENABLE_ON_INIT,
	.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT3),
	.enable_bit	= EN_TC2_CK,
	.recalc		= &followparent_recalc,
};

static struct clk dma_ck = {
	/* No-idle controlled by "tc_ck" */
	.name		= "dma_ck",
	.ops		= &clkops_null,
	.parent		= &tc_ck.clk,
	.recalc		= &followparent_recalc,
};

static struct clk dma_lcdfree_ck = {
	.name		= "dma_lcdfree_ck",
	.ops		= &clkops_null,
	.parent		= &tc_ck.clk,
	.recalc		= &followparent_recalc,
};

static struct arm_idlect1_clk api_ck = {
	.clk = {
		.name		= "api_ck",
		.ops		= &clkops_generic,
		.parent		= &tc_ck.clk,
		.flags		= CLOCK_IDLE_CONTROL,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_APICK,
		.recalc		= &followparent_recalc,
	},
	.idlect_shift	= IDLAPI_ARM_SHIFT,
};

static struct arm_idlect1_clk lb_ck = {
	.clk = {
		.name		= "lb_ck",
		.ops		= &clkops_generic,
		.parent		= &tc_ck.clk,
		.flags		= CLOCK_IDLE_CONTROL,
		.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
		.enable_bit	= EN_LBCK,
		.recalc		= &followparent_recalc,
	},
	.idlect_shift	= IDLLB_ARM_SHIFT,
};

static struct clk rhea1_ck = {
	.name		= "rhea1_ck",
	.ops		= &clkops_null,
	.parent		= &tc_ck.clk,
	.recalc		= &followparent_recalc,
};

static struct clk rhea2_ck = {
	.name		= "rhea2_ck",
	.ops		= &clkops_null,
	.parent		= &tc_ck.clk,
	.recalc		= &followparent_recalc,
};

static struct clk lcd_ck_16xx = {
	.name		= "lcd_ck",
	.ops		= &clkops_generic,
	.parent		= &ck_dpll1,
	.enable_reg	= OMAP1_IO_ADDRESS(ARM_IDLECT2),
	.enable_bit	= EN_LCDCK,
	.rate_offset	= CKCTL_LCDDIV_OFFSET,
	.recalc		= &omap1_ckctl_recalc,
	.round_rate	= omap1_clk_round_rate_ckctl_arm,
	.set_rate	= omap1_clk_set_rate_ckctl_arm,
};

static struct arm_idlect1_clk lcd_ck_1510 = {
	.clk = {
		.name		= "lcd_ck",
		.ops		= &clkops_generic,
		.parent		= &ck_dpll1,
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
 * and 48MHz.  Reimplement with clksel.
 *
 * XXX does this need SYSC register handling?
 */
static struct clk uart1_1510 = {
	.name		= "uart1_ck",
	.ops		= &clkops_null,
	/* Direct from ULPD, no real parent */
	.parent		= &armper_ck.clk,
	.rate		= 12000000,
	.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
	.enable_bit	= CONF_MOD_UART1_CLK_MODE_R,
	.set_rate	= &omap1_set_uart_rate,
	.recalc		= &omap1_uart_recalc,
};

/*
 * XXX The enable_bit here is misused - it simply switches between 12MHz
 * and 48MHz.  Reimplement with clksel.
 *
 * XXX SYSC register handling does not belong in the clock framework
 */
static struct uart_clk uart1_16xx = {
	.clk	= {
		.name		= "uart1_ck",
		.ops		= &clkops_uart_16xx,
		/* Direct from ULPD, no real parent */
		.parent		= &armper_ck.clk,
		.rate		= 48000000,
		.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
		.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
		.enable_bit	= CONF_MOD_UART1_CLK_MODE_R,
	},
	.sysc_addr	= 0xfffb0054,
};

/*
 * XXX The enable_bit here is misused - it simply switches between 12MHz
 * and 48MHz.  Reimplement with clksel.
 *
 * XXX does this need SYSC register handling?
 */
static struct clk uart2_ck = {
	.name		= "uart2_ck",
	.ops		= &clkops_null,
	/* Direct from ULPD, no real parent */
	.parent		= &armper_ck.clk,
	.rate		= 12000000,
	.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
	.enable_bit	= CONF_MOD_UART2_CLK_MODE_R,
	.set_rate	= &omap1_set_uart_rate,
	.recalc		= &omap1_uart_recalc,
};

/*
 * XXX The enable_bit here is misused - it simply switches between 12MHz
 * and 48MHz.  Reimplement with clksel.
 *
 * XXX does this need SYSC register handling?
 */
static struct clk uart3_1510 = {
	.name		= "uart3_ck",
	.ops		= &clkops_null,
	/* Direct from ULPD, no real parent */
	.parent		= &armper_ck.clk,
	.rate		= 12000000,
	.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
	.enable_bit	= CONF_MOD_UART3_CLK_MODE_R,
	.set_rate	= &omap1_set_uart_rate,
	.recalc		= &omap1_uart_recalc,
};

/*
 * XXX The enable_bit here is misused - it simply switches between 12MHz
 * and 48MHz.  Reimplement with clksel.
 *
 * XXX SYSC register handling does not belong in the clock framework
 */
static struct uart_clk uart3_16xx = {
	.clk	= {
		.name		= "uart3_ck",
		.ops		= &clkops_uart_16xx,
		/* Direct from ULPD, no real parent */
		.parent		= &armper_ck.clk,
		.rate		= 48000000,
		.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
		.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
		.enable_bit	= CONF_MOD_UART3_CLK_MODE_R,
	},
	.sysc_addr	= 0xfffb9854,
};

static struct clk usb_clko = {	/* 6 MHz output on W4_USB_CLKO */
	.name		= "usb_clko",
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent */
	.rate		= 6000000,
	.flags		= ENABLE_REG_32BIT,
	.enable_reg	= OMAP1_IO_ADDRESS(ULPD_CLOCK_CTRL),
	.enable_bit	= USB_MCLK_EN_BIT,
};

static struct clk usb_hhc_ck1510 = {
	.name		= "usb_hhc_ck",
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent */
	.rate		= 48000000, /* Actually 2 clocks, 12MHz and 48MHz */
	.flags		= ENABLE_REG_32BIT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
	.enable_bit	= USB_HOST_HHC_UHOST_EN,
};

static struct clk usb_hhc_ck16xx = {
	.name		= "usb_hhc_ck",
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent */
	.rate		= 48000000,
	/* OTG_SYSCON_2.OTG_PADEN == 0 (not 1510-compatible) */
	.flags		= ENABLE_REG_32BIT,
	.enable_reg	= OMAP1_IO_ADDRESS(OTG_BASE + 0x08), /* OTG_SYSCON_2 */
	.enable_bit	= OTG_SYSCON_2_UHOST_EN_SHIFT
};

static struct clk usb_dc_ck = {
	.name		= "usb_dc_ck",
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent */
	.rate		= 48000000,
	.enable_reg	= OMAP1_IO_ADDRESS(SOFT_REQ_REG),
	.enable_bit	= SOFT_USB_OTG_DPLL_REQ_SHIFT,
};

static struct clk uart1_7xx = {
	.name		= "uart1_ck",
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent */
	.rate		= 12000000,
	.enable_reg	= OMAP1_IO_ADDRESS(SOFT_REQ_REG),
	.enable_bit	= 9,
};

static struct clk uart2_7xx = {
	.name		= "uart2_ck",
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent */
	.rate		= 12000000,
	.enable_reg	= OMAP1_IO_ADDRESS(SOFT_REQ_REG),
	.enable_bit	= 11,
};

static struct clk mclk_1510 = {
	.name		= "mclk",
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.rate		= 12000000,
	.enable_reg	= OMAP1_IO_ADDRESS(SOFT_REQ_REG),
	.enable_bit	= SOFT_COM_MCKO_REQ_SHIFT,
};

static struct clk mclk_16xx = {
	.name		= "mclk",
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.enable_reg	= OMAP1_IO_ADDRESS(COM_CLK_DIV_CTRL_SEL),
	.enable_bit	= COM_ULPD_PLL_CLK_REQ,
	.set_rate	= &omap1_set_ext_clk_rate,
	.round_rate	= &omap1_round_ext_clk_rate,
	.init		= &omap1_init_ext_clk,
};

static struct clk bclk_1510 = {
	.name		= "bclk",
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.rate		= 12000000,
};

static struct clk bclk_16xx = {
	.name		= "bclk",
	.ops		= &clkops_generic,
	/* Direct from ULPD, no parent. May be enabled by ext hardware. */
	.enable_reg	= OMAP1_IO_ADDRESS(SWD_CLK_DIV_CTRL_SEL),
	.enable_bit	= SWD_ULPD_PLL_CLK_REQ,
	.set_rate	= &omap1_set_ext_clk_rate,
	.round_rate	= &omap1_round_ext_clk_rate,
	.init		= &omap1_init_ext_clk,
};

static struct clk mmc1_ck = {
	.name		= "mmc1_ck",
	.ops		= &clkops_generic,
	/* Functional clock is direct from ULPD, interface clock is ARMPER */
	.parent		= &armper_ck.clk,
	.rate		= 48000000,
	.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
	.enable_bit	= CONF_MOD_MMC_SD_CLK_REQ_R,
};

/*
 * XXX MOD_CONF_CTRL_0 bit 20 is defined in the 1510 TRM as
 * CONF_MOD_MCBSP3_AUXON ??
 */
static struct clk mmc2_ck = {
	.name		= "mmc2_ck",
	.ops		= &clkops_generic,
	/* Functional clock is direct from ULPD, interface clock is ARMPER */
	.parent		= &armper_ck.clk,
	.rate		= 48000000,
	.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= OMAP1_IO_ADDRESS(MOD_CONF_CTRL_0),
	.enable_bit	= 20,
};

static struct clk mmc3_ck = {
	.name		= "mmc3_ck",
	.ops		= &clkops_generic,
	/* Functional clock is direct from ULPD, interface clock is ARMPER */
	.parent		= &armper_ck.clk,
	.rate		= 48000000,
	.flags		= ENABLE_REG_32BIT | CLOCK_NO_IDLE_PARENT,
	.enable_reg	= OMAP1_IO_ADDRESS(SOFT_REQ_REG),
	.enable_bit	= SOFT_MMC_DPLL_REQ_SHIFT,
};

static struct clk virtual_ck_mpu = {
	.name		= "mpu",
	.ops		= &clkops_null,
	.parent		= &arm_ck, /* Is smarter alias for */
	.recalc		= &followparent_recalc,
	.set_rate	= &omap1_select_table_rate,
	.round_rate	= &omap1_round_to_table_rate,
};

/* virtual functional clock domain for I2C. Just for making sure that ARMXOR_CK
remains active during MPU idle whenever this is enabled */
static struct clk i2c_fck = {
	.name		= "i2c_fck",
	.ops		= &clkops_null,
	.flags		= CLOCK_NO_IDLE_PARENT,
	.parent		= &armxor_ck.clk,
	.recalc		= &followparent_recalc,
};

static struct clk i2c_ick = {
	.name		= "i2c_ick",
	.ops		= &clkops_null,
	.flags		= CLOCK_NO_IDLE_PARENT,
	.parent		= &armper_ck.clk,
	.recalc		= &followparent_recalc,
};

/*
 * clkdev integration
 */

static struct omap_clk omap_clks[] = {
	/* non-ULPD clocks */
	CLK(NULL,	"ck_ref",	&ck_ref,	CK_16XX | CK_1510 | CK_310 | CK_7XX),
	CLK(NULL,	"ck_dpll1",	&ck_dpll1,	CK_16XX | CK_1510 | CK_310 | CK_7XX),
	/* CK_GEN1 clocks */
	CLK(NULL,	"ck_dpll1out",	&ck_dpll1out.clk, CK_16XX),
	CLK(NULL,	"ck_sossi",	&sossi_ck,	CK_16XX),
	CLK(NULL,	"arm_ck",	&arm_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"armper_ck",	&armper_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK("omap_gpio.0", "ick",	&arm_gpio_ck,	CK_1510 | CK_310),
	CLK(NULL,	"armxor_ck",	&armxor_ck.clk,	CK_16XX | CK_1510 | CK_310 | CK_7XX),
	CLK(NULL,	"armtim_ck",	&armtim_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK("omap_wdt",	"fck",		&armwdt_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK("omap_wdt",	"ick",		&armper_ck.clk,	CK_16XX),
	CLK("omap_wdt", "ick",		&dummy_ck,	CK_1510 | CK_310),
	CLK(NULL,	"arminth_ck",	&arminth_ck1510, CK_1510 | CK_310),
	CLK(NULL,	"arminth_ck",	&arminth_ck16xx, CK_16XX),
	/* CK_GEN2 clocks */
	CLK(NULL,	"dsp_ck",	&dsp_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dspmmu_ck",	&dspmmu_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dspper_ck",	&dspper_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dspxor_ck",	&dspxor_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dsptim_ck",	&dsptim_ck,	CK_16XX | CK_1510 | CK_310),
	/* CK_GEN3 clocks */
	CLK(NULL,	"tc_ck",	&tc_ck.clk,	CK_16XX | CK_1510 | CK_310 | CK_7XX),
	CLK(NULL,	"tipb_ck",	&tipb_ck,	CK_1510 | CK_310),
	CLK(NULL,	"l3_ocpi_ck",	&l3_ocpi_ck,	CK_16XX | CK_7XX),
	CLK(NULL,	"tc1_ck",	&tc1_ck,	CK_16XX),
	CLK(NULL,	"tc2_ck",	&tc2_ck,	CK_16XX),
	CLK(NULL,	"dma_ck",	&dma_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"dma_lcdfree_ck", &dma_lcdfree_ck, CK_16XX),
	CLK(NULL,	"api_ck",	&api_ck.clk,	CK_16XX | CK_1510 | CK_310 | CK_7XX),
	CLK(NULL,	"lb_ck",	&lb_ck.clk,	CK_1510 | CK_310),
	CLK(NULL,	"rhea1_ck",	&rhea1_ck,	CK_16XX),
	CLK(NULL,	"rhea2_ck",	&rhea2_ck,	CK_16XX),
	CLK(NULL,	"lcd_ck",	&lcd_ck_16xx,	CK_16XX | CK_7XX),
	CLK(NULL,	"lcd_ck",	&lcd_ck_1510.clk, CK_1510 | CK_310),
	/* ULPD clocks */
	CLK(NULL,	"uart1_ck",	&uart1_1510,	CK_1510 | CK_310),
	CLK(NULL,	"uart1_ck",	&uart1_16xx.clk, CK_16XX),
	CLK(NULL,	"uart1_ck",	&uart1_7xx,	CK_7XX),
	CLK(NULL,	"uart2_ck",	&uart2_ck,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"uart2_ck",	&uart2_7xx,	CK_7XX),
	CLK(NULL,	"uart3_ck",	&uart3_1510,	CK_1510 | CK_310),
	CLK(NULL,	"uart3_ck",	&uart3_16xx.clk, CK_16XX),
	CLK(NULL,	"usb_clko",	&usb_clko,	CK_16XX | CK_1510 | CK_310),
	CLK(NULL,	"usb_hhc_ck",	&usb_hhc_ck1510, CK_1510 | CK_310),
	CLK(NULL,	"usb_hhc_ck",	&usb_hhc_ck16xx, CK_16XX),
	CLK(NULL,	"usb_dc_ck",	&usb_dc_ck,	CK_16XX | CK_7XX),
	CLK(NULL,	"mclk",		&mclk_1510,	CK_1510 | CK_310),
	CLK(NULL,	"mclk",		&mclk_16xx,	CK_16XX),
	CLK(NULL,	"bclk",		&bclk_1510,	CK_1510 | CK_310),
	CLK(NULL,	"bclk",		&bclk_16xx,	CK_16XX),
	CLK("mmci-omap.0", "fck",	&mmc1_ck,	CK_16XX | CK_1510 | CK_310),
	CLK("mmci-omap.0", "fck",	&mmc3_ck,	CK_7XX),
	CLK("mmci-omap.0", "ick",	&armper_ck.clk,	CK_16XX | CK_1510 | CK_310 | CK_7XX),
	CLK("mmci-omap.1", "fck",	&mmc2_ck,	CK_16XX),
	CLK("mmci-omap.1", "ick",	&armper_ck.clk,	CK_16XX),
	/* Virtual clocks */
	CLK(NULL,	"mpu",		&virtual_ck_mpu, CK_16XX | CK_1510 | CK_310),
	CLK("omap_i2c.1", "fck",	&i2c_fck,	CK_16XX | CK_1510 | CK_310 | CK_7XX),
	CLK("omap_i2c.1", "ick",	&i2c_ick,	CK_16XX),
	CLK("omap_i2c.1", "ick",	&dummy_ck,	CK_1510 | CK_310 | CK_7XX),
	CLK("omap1_spi100k.1", "fck",	&dummy_ck,	CK_7XX),
	CLK("omap1_spi100k.1", "ick",	&dummy_ck,	CK_7XX),
	CLK("omap1_spi100k.2", "fck",	&dummy_ck,	CK_7XX),
	CLK("omap1_spi100k.2", "ick",	&dummy_ck,	CK_7XX),
	CLK("omap_uwire", "fck",	&armxor_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK("omap-mcbsp.1", "ick",	&dspper_ck,	CK_16XX),
	CLK("omap-mcbsp.1", "ick",	&dummy_ck,	CK_1510 | CK_310),
	CLK("omap-mcbsp.2", "ick",	&armper_ck.clk,	CK_16XX),
	CLK("omap-mcbsp.2", "ick",	&dummy_ck,	CK_1510 | CK_310),
	CLK("omap-mcbsp.3", "ick",	&dspper_ck,	CK_16XX),
	CLK("omap-mcbsp.3", "ick",	&dummy_ck,	CK_1510 | CK_310),
	CLK("omap-mcbsp.1", "fck",	&dspxor_ck,	CK_16XX | CK_1510 | CK_310),
	CLK("omap-mcbsp.2", "fck",	&armper_ck.clk,	CK_16XX | CK_1510 | CK_310),
	CLK("omap-mcbsp.3", "fck",	&dspxor_ck,	CK_16XX | CK_1510 | CK_310),
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
	int crystal_type = 0; /* Default 12 MHz */
	u32 reg;

#ifdef CONFIG_DEBUG_LL
	/*
	 * Resets some clocks that may be left on from bootloader,
	 * but leaves serial clocks on.
	 */
	omap_writel(0x3 << 29, MOD_CONF_CTRL_0);
#endif

	/* USB_REQ_EN will be disabled later if necessary (usb_dc_ck) */
	reg = omap_readw(SOFT_REQ_REG) & (1 << 4);
	omap_writew(reg, SOFT_REQ_REG);
	if (!cpu_is_omap15xx())
		omap_writew(0, SOFT_REQ_REG2);

	/* By default all idlect1 clocks are allowed to idle */
	arm_idlect1_mask = ~0;

	for (c = omap_clks; c < omap_clks + ARRAY_SIZE(omap_clks); c++)
		clk_preinit(c->lk.clk);

	cpu_mask = 0;
	if (cpu_is_omap1710())
		cpu_mask |= CK_1710;
	if (cpu_is_omap16xx())
		cpu_mask |= CK_16XX;
	if (cpu_is_omap1510())
		cpu_mask |= CK_1510;
	if (cpu_is_omap7xx())
		cpu_mask |= CK_7XX;
	if (cpu_is_omap310())
		cpu_mask |= CK_310;

	for (c = omap_clks; c < omap_clks + ARRAY_SIZE(omap_clks); c++)
		if (c->cpu & cpu_mask) {
			clkdev_add(&c->lk);
			clk_register(c->lk.clk);
		}

	/* Pointers to these clocks are needed by code in clock.c */
	api_ck_p = clk_get(NULL, "api_ck");
	ck_dpll1_p = clk_get(NULL, "ck_dpll1");
	ck_ref_p = clk_get(NULL, "ck_ref");

	if (cpu_is_omap7xx())
		ck_ref.rate = 13000000;
	if (cpu_is_omap16xx() && crystal_type == 2)
		ck_ref.rate = 19200000;

	pr_info("Clocks: ARM_SYSST: 0x%04x DPLL_CTL: 0x%04x ARM_CKCTL: 0x%04x\n",
		omap_readw(ARM_SYSST), omap_readw(DPLL_CTL),
		omap_readw(ARM_CKCTL));

	/* We want to be in syncronous scalable mode */
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
	propagate_rate(&ck_dpll1);
	/* Cache rates for clocks connected to ck_ref (not dpll1) */
	propagate_rate(&ck_ref);
	omap1_show_rates();
	if (machine_is_omap_perseus2() || machine_is_omap_fsample()) {
		/* Select slicer output as OMAP input clock */
		omap_writew(omap_readw(OMAP7XX_PCC_UPLD_CTRL) & ~0x1,
				OMAP7XX_PCC_UPLD_CTRL);
	}

	/* Amstrad Delta wants BCLK high when inactive */
	if (machine_is_ams_delta())
		omap_writel(omap_readl(ULPD_CLOCK_CTRL) |
				(1 << SDW_MCLK_INV_BIT),
				ULPD_CLOCK_CTRL);

	/* Turn off DSP and ARM_TIMXO. Make sure ARM_INTHCK is not divided */
	/* (on 730, bit 13 must not be cleared) */
	if (cpu_is_omap7xx())
		omap_writew(omap_readw(ARM_CKCTL) & 0x2fff, ARM_CKCTL);
	else
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

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	clk_enable(&armper_ck.clk);
	clk_enable(&armxor_ck.clk);
	clk_enable(&armtim_ck.clk); /* This should be done by timer code */

	if (cpu_is_omap15xx())
		clk_enable(&arm_gpio_ck);

	return 0;
}

#define OMAP1_DPLL1_SANE_VALUE	60000000

void __init omap1_clk_late_init(void)
{
	unsigned long rate = ck_dpll1.rate;

	/* Find the highest supported frequency and enable it */
	if (omap1_select_table_rate(&virtual_ck_mpu, ~0)) {
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
