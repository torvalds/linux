/*
 *  linux/arch/arm/mach-omap1/clock.h
 *
 *  Copyright (C) 2004 - 2005, 2009 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *  Based on clocks.h by Tony Lindgren, Gordon McNutt and RidgeRun, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP1_CLOCK_H
#define __ARCH_ARM_MACH_OMAP1_CLOCK_H

#include <linux/clk.h>

#include <plat/clock.h>

int omap1_clk_init(void);
void omap1_clk_late_init(void);
extern int omap1_clk_enable(struct clk *clk);
extern void omap1_clk_disable(struct clk *clk);
extern long omap1_clk_round_rate(struct clk *clk, unsigned long rate);
extern int omap1_clk_set_rate(struct clk *clk, unsigned long rate);
extern unsigned long omap1_ckctl_recalc(struct clk *clk);
extern int omap1_set_sossi_rate(struct clk *clk, unsigned long rate);
extern unsigned long omap1_sossi_recalc(struct clk *clk);
extern unsigned long omap1_ckctl_recalc_dsp_domain(struct clk *clk);
extern int omap1_clk_set_rate_dsp_domain(struct clk *clk, unsigned long rate);
extern int omap1_set_uart_rate(struct clk *clk, unsigned long rate);
extern unsigned long omap1_uart_recalc(struct clk *clk);
extern int omap1_set_ext_clk_rate(struct clk *clk, unsigned long rate);
extern long omap1_round_ext_clk_rate(struct clk *clk, unsigned long rate);
extern void omap1_init_ext_clk(struct clk *clk);
extern int omap1_select_table_rate(struct clk *clk, unsigned long rate);
extern long omap1_round_to_table_rate(struct clk *clk, unsigned long rate);
extern int omap1_clk_set_rate_ckctl_arm(struct clk *clk, unsigned long rate);
extern long omap1_clk_round_rate_ckctl_arm(struct clk *clk, unsigned long rate);
extern unsigned long omap1_watchdog_recalc(struct clk *clk);

#ifdef CONFIG_OMAP_RESET_CLOCKS
extern void omap1_clk_disable_unused(struct clk *clk);
#else
#define omap1_clk_disable_unused	NULL
#endif

struct uart_clk {
	struct clk	clk;
	unsigned long	sysc_addr;
};

/* Provide a method for preventing idling some ARM IDLECT clocks */
struct arm_idlect1_clk {
	struct clk	clk;
	unsigned long	no_idle_count;
	__u8		idlect_shift;
};

/* ARM_CKCTL bit shifts */
#define CKCTL_PERDIV_OFFSET	0
#define CKCTL_LCDDIV_OFFSET	2
#define CKCTL_ARMDIV_OFFSET	4
#define CKCTL_DSPDIV_OFFSET	6
#define CKCTL_TCDIV_OFFSET	8
#define CKCTL_DSPMMUDIV_OFFSET	10
/*#define ARM_TIMXO		12*/
#define EN_DSPCK		13
/*#define ARM_INTHCK_SEL	14*/ /* Divide-by-2 for mpu inth_ck */
/* DSP_CKCTL bit shifts */
#define CKCTL_DSPPERDIV_OFFSET	0

/* ARM_IDLECT2 bit shifts */
#define EN_WDTCK	0
#define EN_XORPCK	1
#define EN_PERCK	2
#define EN_LCDCK	3
#define EN_LBCK		4 /* Not on 1610/1710 */
/*#define EN_HSABCK	5*/
#define EN_APICK	6
#define EN_TIMCK	7
#define DMACK_REQ	8
#define EN_GPIOCK	9 /* Not on 1610/1710 */
/*#define EN_LBFREECK	10*/
#define EN_CKOUT_ARM	11

/* ARM_IDLECT3 bit shifts */
#define EN_OCPI_CK	0
#define EN_TC1_CK	2
#define EN_TC2_CK	4

/* DSP_IDLECT2 bit shifts (0,1,2 are same as for ARM_IDLECT2) */
#define EN_DSPTIMCK	5

/* Various register defines for clock controls scattered around OMAP chip */
#define SDW_MCLK_INV_BIT	2	/* In ULPD_CLKC_CTRL */
#define USB_MCLK_EN_BIT		4	/* In ULPD_CLKC_CTRL */
#define USB_HOST_HHC_UHOST_EN	9	/* In MOD_CONF_CTRL_0 */
#define SWD_ULPD_PLL_CLK_REQ	1	/* In SWD_CLK_DIV_CTRL_SEL */
#define COM_ULPD_PLL_CLK_REQ	1	/* In COM_CLK_DIV_CTRL_SEL */
#define SWD_CLK_DIV_CTRL_SEL	0xfffe0874
#define COM_CLK_DIV_CTRL_SEL	0xfffe0878
#define SOFT_REQ_REG		0xfffe0834
#define SOFT_REQ_REG2		0xfffe0880

extern __u32 arm_idlect1_mask;
extern struct clk *api_ck_p, *ck_dpll1_p, *ck_ref_p;

extern const struct clkops clkops_dspck;
extern const struct clkops clkops_dummy;
extern const struct clkops clkops_uart_16xx;
extern const struct clkops clkops_generic;

#endif
