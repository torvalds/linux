/*
 * OMAP3 clock function prototypes and macros
 *
 * Copyright (C) 2007-2009 Texas Instruments, Inc.
 * Copyright (C) 2007-2009 Nokia Corporation
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLOCK_34XX_H
#define __ARCH_ARM_MACH_OMAP2_CLOCK_34XX_H

int omap3_dpll4_set_rate(struct clk *clk, unsigned long rate);
int omap3_core_dpll_m2_set_rate(struct clk *clk, unsigned long rate);
void omap3_clk_lock_dpll5(void);

extern struct clk *sdrc_ick_p;
extern struct clk *arm_fck_p;

/* OMAP34xx-specific clkops */
extern const struct clkops clkops_omap3430es2_ssi_wait;
extern const struct clkops clkops_omap3430es2_hsotgusb_wait;
extern const struct clkops clkops_omap3430es2_dss_usbhost_wait;
extern const struct clkops clkops_noncore_dpll_ops;

#endif
