/*
 *  linux/arch/arm/mach-omap2/clock.h
 *
 *  Copyright (C) 2005-2009 Texas Instruments, Inc.
 *  Copyright (C) 2004-2011 Nokia Corporation
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

#include <linux/kernel.h>

#include <plat/clock.h>

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

/* OMAP4xxx CM_CLKMODE_DPLL*.EN_*_DPLL bits - for omap2_get_dpll_rate() */
#define OMAP4XXX_EN_DPLL_MNBYPASS		0x4
#define OMAP4XXX_EN_DPLL_LPBYPASS		0x5
#define OMAP4XXX_EN_DPLL_FRBYPASS		0x6
#define OMAP4XXX_EN_DPLL_LOCKED			0x7

/* CM_CLKEN_PLL*.EN* bit values - not all are available for every DPLL */
#define DPLL_LOW_POWER_STOP	0x1
#define DPLL_LOW_POWER_BYPASS	0x5
#define DPLL_LOCKED		0x7

/* DPLL Type and DCO Selection Flags */
#define DPLL_J_TYPE		0x1

int omap2_clk_enable(struct clk *clk);
void omap2_clk_disable(struct clk *clk);
long omap2_clk_round_rate(struct clk *clk, unsigned long rate);
int omap2_clk_set_rate(struct clk *clk, unsigned long rate);
int omap2_clk_set_parent(struct clk *clk, struct clk *new_parent);
long omap2_dpll_round_rate(struct clk *clk, unsigned long target_rate);
unsigned long omap3_dpll_recalc(struct clk *clk);
unsigned long omap3_clkoutx2_recalc(struct clk *clk);
void omap3_dpll_allow_idle(struct clk *clk);
void omap3_dpll_deny_idle(struct clk *clk);
u32 omap3_dpll_autoidle_read(struct clk *clk);
int omap3_noncore_dpll_set_rate(struct clk *clk, unsigned long rate);
int omap3_noncore_dpll_enable(struct clk *clk);
void omap3_noncore_dpll_disable(struct clk *clk);
int omap4_dpllmx_gatectrl_read(struct clk *clk);
void omap4_dpllmx_allow_gatectrl(struct clk *clk);
void omap4_dpllmx_deny_gatectrl(struct clk *clk);
long omap4_dpll_regm4xen_round_rate(struct clk *clk, unsigned long target_rate);
unsigned long omap4_dpll_regm4xen_recalc(struct clk *clk);

#ifdef CONFIG_OMAP_RESET_CLOCKS
void omap2_clk_disable_unused(struct clk *clk);
#else
#define omap2_clk_disable_unused	NULL
#endif

void omap2_init_clk_clkdm(struct clk *clk);
void __init omap2_clk_disable_clkdm_control(void);

/* clkt_clksel.c public functions */
u32 omap2_clksel_round_rate_div(struct clk *clk, unsigned long target_rate,
				u32 *new_div);
void omap2_init_clksel_parent(struct clk *clk);
unsigned long omap2_clksel_recalc(struct clk *clk);
long omap2_clksel_round_rate(struct clk *clk, unsigned long target_rate);
int omap2_clksel_set_rate(struct clk *clk, unsigned long rate);
int omap2_clksel_set_parent(struct clk *clk, struct clk *new_parent);

/* clkt_iclk.c public functions */
extern void omap2_clkt_iclk_allow_idle(struct clk *clk);
extern void omap2_clkt_iclk_deny_idle(struct clk *clk);

u32 omap2_get_dpll_rate(struct clk *clk);
void omap2_init_dpll_parent(struct clk *clk);

int omap2_wait_clock_ready(void __iomem *reg, u32 cval, const char *name);


#ifdef CONFIG_ARCH_OMAP2
void omap2xxx_clk_prepare_for_reboot(void);
#else
static inline void omap2xxx_clk_prepare_for_reboot(void)
{
}
#endif

#ifdef CONFIG_ARCH_OMAP3
void omap3_clk_prepare_for_reboot(void);
#else
static inline void omap3_clk_prepare_for_reboot(void)
{
}
#endif

#ifdef CONFIG_ARCH_OMAP4
void omap4_clk_prepare_for_reboot(void);
#else
static inline void omap4_clk_prepare_for_reboot(void)
{
}
#endif

int omap2_dflt_clk_enable(struct clk *clk);
void omap2_dflt_clk_disable(struct clk *clk);
void omap2_clk_dflt_find_companion(struct clk *clk, void __iomem **other_reg,
				   u8 *other_bit);
void omap2_clk_dflt_find_idlest(struct clk *clk, void __iomem **idlest_reg,
				u8 *idlest_bit, u8 *idlest_val);
int omap2_clk_switch_mpurate_at_boot(const char *mpurate_ck_name);
void omap2_clk_print_new_rates(const char *hfclkin_ck_name,
			       const char *core_ck_name,
			       const char *mpu_ck_name);

extern u16 cpu_mask;

extern const struct clkops clkops_omap2_dflt_wait;
extern const struct clkops clkops_dummy;
extern const struct clkops clkops_omap2_dflt;

extern struct clk_functions omap2_clk_functions;
extern struct clk *vclk, *sclk;

extern const struct clksel_rate gpt_32k_rates[];
extern const struct clksel_rate gpt_sys_rates[];
extern const struct clksel_rate gfx_l3_rates[];
extern const struct clksel_rate dsp_ick_rates[];

extern const struct clkops clkops_omap2_iclk_dflt_wait;
extern const struct clkops clkops_omap2_iclk_dflt;
extern const struct clkops clkops_omap2_iclk_idle_only;
extern const struct clkops clkops_omap2_mdmclk_dflt_wait;
extern const struct clkops clkops_omap2xxx_dpll_ops;
extern const struct clkops clkops_omap3_noncore_dpll_ops;
extern const struct clkops clkops_omap3_core_dpll_ops;
extern const struct clkops clkops_omap4_dpllmx_ops;

#endif
