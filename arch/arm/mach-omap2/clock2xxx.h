/*
 * OMAP2 clock function prototypes and macros
 *
 * Copyright (C) 2005-2009 Texas Instruments, Inc.
 * Copyright (C) 2004-2009 Nokia Corporation
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLOCK_24XX_H
#define __ARCH_ARM_MACH_OMAP2_CLOCK_24XX_H

unsigned long omap2_table_mpu_recalc(struct clk *clk);
int omap2_select_table_rate(struct clk *clk, unsigned long rate);
long omap2_round_to_table_rate(struct clk *clk, unsigned long rate);
unsigned long omap2_sys_clk_recalc(struct clk *clk);
unsigned long omap2_osc_clk_recalc(struct clk *clk);
unsigned long omap2_sys_clk_recalc(struct clk *clk);
unsigned long omap2_dpllcore_recalc(struct clk *clk);
int omap2_reprogram_dpllcore(struct clk *clk, unsigned long rate);
unsigned long omap2xxx_clk_get_core_rate(struct clk *clk);

/* REVISIT: These should be set dynamically for CONFIG_MULTI_OMAP2 */
#ifdef CONFIG_ARCH_OMAP2420
#define OMAP_CM_REGADDR			OMAP2420_CM_REGADDR
#define OMAP24XX_PRCM_CLKOUT_CTRL	OMAP2420_PRCM_CLKOUT_CTRL
#define OMAP24XX_PRCM_CLKEMUL_CTRL	OMAP2420_PRCM_CLKEMUL_CTRL
#else
#define OMAP_CM_REGADDR			OMAP2430_CM_REGADDR
#define OMAP24XX_PRCM_CLKOUT_CTRL	OMAP2430_PRCM_CLKOUT_CTRL
#define OMAP24XX_PRCM_CLKEMUL_CTRL	OMAP2430_PRCM_CLKEMUL_CTRL
#endif

extern void __iomem *prcm_clksrc_ctrl;

extern struct clk *dclk;

extern const struct clkops clkops_omap2430_i2chs_wait;
extern const struct clkops clkops_oscck;
extern const struct clkops clkops_apll96;
extern const struct clkops clkops_apll54;

#endif
