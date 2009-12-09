/*
 * OMAP4 clock function prototypes and macros
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLOCK_44XX_H
#define __ARCH_ARM_MACH_OMAP2_CLOCK_44XX_H

unsigned long omap3_dpll_recalc(struct clk *clk);
unsigned long omap3_clkoutx2_recalc(struct clk *clk);
int omap3_noncore_dpll_set_rate(struct clk *clk, unsigned long rate);

/* DPLL modes */
#define DPLL_LOW_POWER_STOP	0x1
#define DPLL_LOW_POWER_BYPASS	0x5
#define DPLL_LOCKED		0x7
#define OMAP4430_MAX_DPLL_MULT	2048
#define OMAP4430_MAX_DPLL_DIV	128

extern const struct clkops clkops_noncore_dpll_ops;

#endif
