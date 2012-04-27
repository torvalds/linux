/*
 * OMAP36xx clock function prototypes and macros
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLOCK36XX_H
#define __ARCH_ARM_MACH_OMAP2_CLOCK36XX_H

#ifdef CONFIG_COMMON_CLK
extern int omap36xx_pwrdn_clk_enable_with_hsdiv_restore(struct clk_hw *hw);
#else
extern const struct clkops clkops_omap36xx_pwrdn_with_hsdiv_wait_restore;
#endif

#endif
