/*
 *  linux/arch/arm/mach-omap2/clock.h
 *
 *  Copyright (C) 2005-2008 Texas Instruments, Inc.
 *  Copyright (C) 2004-2008 Nokia Corporation
 *
 *  Contacts:
 *  Richard Woodruff <r-woodruff2@ti.com>
 *  Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLOCK_H
#define __ARCH_ARM_MACH_OMAP2_CLOCK_H

#include <mach/clock.h>

/* The maximum error between a target DPLL rate and the rounded rate in Hz */
#define DEFAULT_DPLL_RATE_TOLERANCE	50000

/* CM_CLKSEL2_PLL.CORE_CLK_SRC bits (2XXX) */
#define CORE_CLK_SRC_32K		0x0
#define CORE_CLK_SRC_DPLL		0x1
#define CORE_CLK_SRC_DPLL_X2		0x2

/* OMAP2xxx CM_CLKEN_PLL.EN_DPLL bits - for omap2_get_dpll_rate() */
#define OMAP2XXX_EN_DPLL_LPBYPASS		0x1
#define OMAP2XXX_EN_DPLL_FRBYPASS		0x2
#define OMAP2XXX_EN_DPLL_LOCKED			0x3

/* OMAP3xxx CM_CLKEN_PLL*.EN_*_DPLL bits - for omap2_get_dpll_rate() */
#define OMAP3XXX_EN_DPLL_LPBYPASS		0x5
#define OMAP3XXX_EN_DPLL_FRBYPASS		0x6
#define OMAP3XXX_EN_DPLL_LOCKED			0x7

int omap2_clk_init(void);
int omap2_clk_enable(struct clk *clk);
void omap2_clk_disable(struct clk *clk);
long omap2_clk_round_rate(struct clk *clk, unsigned long rate);
int omap2_clk_set_rate(struct clk *clk, unsigned long rate);
int omap2_clk_set_parent(struct clk *clk, struct clk *new_parent);
int omap2_dpll_set_rate_tolerance(struct clk *clk, unsigned int tolerance);
long omap2_dpll_round_rate(struct clk *clk, unsigned long target_rate);

#ifdef CONFIG_OMAP_RESET_CLOCKS
void omap2_clk_disable_unused(struct clk *clk);
#else
#define omap2_clk_disable_unused	NULL
#endif

unsigned long omap2_clksel_recalc(struct clk *clk);
void omap2_init_clk_clkdm(struct clk *clk);
void omap2_init_clksel_parent(struct clk *clk);
u32 omap2_clksel_get_divisor(struct clk *clk);
u32 omap2_clksel_round_rate_div(struct clk *clk, unsigned long target_rate,
				u32 *new_div);
u32 omap2_clksel_to_divisor(struct clk *clk, u32 field_val);
u32 omap2_divisor_to_clksel(struct clk *clk, u32 div);
unsigned long omap2_fixed_divisor_recalc(struct clk *clk);
long omap2_clksel_round_rate(struct clk *clk, unsigned long target_rate);
int omap2_clksel_set_rate(struct clk *clk, unsigned long rate);
u32 omap2_get_dpll_rate(struct clk *clk);
int omap2_wait_clock_ready(void __iomem *reg, u32 cval, const char *name);
void omap2_clk_prepare_for_reboot(void);
int omap2_dflt_clk_enable(struct clk *clk);
void omap2_dflt_clk_disable(struct clk *clk);
void omap2_clk_dflt_find_companion(struct clk *clk, void __iomem **other_reg,
				   u8 *other_bit);
void omap2_clk_dflt_find_idlest(struct clk *clk, void __iomem **idlest_reg,
				u8 *idlest_bit);

extern const struct clkops clkops_omap2_dflt_wait;
extern const struct clkops clkops_omap2_dflt;

extern u8 cpu_mask;

/* clksel_rate data common to 24xx/343x */
static const struct clksel_rate gpt_32k_rates[] = {
	 { .div = 1, .val = 0, .flags = RATE_IN_24XX | RATE_IN_343X | DEFAULT_RATE },
	 { .div = 0 }
};

static const struct clksel_rate gpt_sys_rates[] = {
	 { .div = 1, .val = 1, .flags = RATE_IN_24XX | RATE_IN_343X | DEFAULT_RATE },
	 { .div = 0 }
};

static const struct clksel_rate gfx_l3_rates[] = {
	{ .div = 1, .val = 1, .flags = RATE_IN_24XX | RATE_IN_343X },
	{ .div = 2, .val = 2, .flags = RATE_IN_24XX | RATE_IN_343X | DEFAULT_RATE },
	{ .div = 3, .val = 3, .flags = RATE_IN_243X | RATE_IN_343X },
	{ .div = 4, .val = 4, .flags = RATE_IN_243X | RATE_IN_343X },
	{ .div = 0 }
};


#endif
