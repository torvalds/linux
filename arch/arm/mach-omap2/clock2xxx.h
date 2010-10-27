/*
 * OMAP2 clock function prototypes and macros
 *
 * Copyright (C) 2005-2010 Texas Instruments, Inc.
 * Copyright (C) 2004-2010 Nokia Corporation
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLOCK2XXX_H
#define __ARCH_ARM_MACH_OMAP2_CLOCK2XXX_H

unsigned long omap2_table_mpu_recalc(struct clk *clk);
int omap2_select_table_rate(struct clk *clk, unsigned long rate);
long omap2_round_to_table_rate(struct clk *clk, unsigned long rate);
unsigned long omap2xxx_sys_clk_recalc(struct clk *clk);
unsigned long omap2_osc_clk_recalc(struct clk *clk);
unsigned long omap2_dpllcore_recalc(struct clk *clk);
int omap2_reprogram_dpllcore(struct clk *clk, unsigned long rate);
unsigned long omap2xxx_clk_get_core_rate(struct clk *clk);
u32 omap2xxx_get_apll_clkin(void);
u32 omap2xxx_get_sysclkdiv(void);
void omap2xxx_clk_prepare_for_reboot(void);

#ifdef CONFIG_ARCH_OMAP2420
int omap2420_clk_init(void);
#else
#define omap2420_clk_init()	0
#endif

#ifdef CONFIG_ARCH_OMAP2430
int omap2430_clk_init(void);
#else
#define omap2430_clk_init()	0
#endif

extern void __iomem *prcm_clksrc_ctrl, *cm_idlest_pll;

extern struct clk *dclk;

extern const struct clkops clkops_omap2430_i2chs_wait;
extern const struct clkops clkops_oscck;
extern const struct clkops clkops_apll96;
extern const struct clkops clkops_apll54;

#endif
