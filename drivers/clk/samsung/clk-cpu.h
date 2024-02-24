/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *
 * Common Clock Framework support for all PLL's in Samsung platforms
*/

#ifndef __SAMSUNG_CLK_CPU_H
#define __SAMSUNG_CLK_CPU_H

/* The CPU clock registers have DIV1 configuration register */
#define CLK_CPU_HAS_DIV1		BIT(0)
/* When ALT parent is active, debug clocks need safe divider values */
#define CLK_CPU_NEEDS_DEBUG_ALT_DIV	BIT(1)
/* The CPU clock registers have Exynos5433-compatible layout */
#define CLK_CPU_HAS_E5433_REGS_LAYOUT	BIT(2)

/**
 * struct exynos_cpuclk_cfg_data - config data to setup cpu clocks
 * @prate: frequency of the primary parent clock (in KHz)
 * @div0: value to be programmed in the div_cpu0 register
 * @div1: value to be programmed in the div_cpu1 register
 *
 * This structure holds the divider configuration data for dividers in the CPU
 * clock domain. The parent frequency at which these divider values are valid is
 * specified in @prate. The @prate is the frequency of the primary parent clock.
 * For CPU clock domains that do not have a DIV1 register, the @div1 member
 * value is not used.
 */
struct exynos_cpuclk_cfg_data {
	unsigned long	prate;
	unsigned long	div0;
	unsigned long	div1;
};

#endif /* __SAMSUNG_CLK_CPU_H */
