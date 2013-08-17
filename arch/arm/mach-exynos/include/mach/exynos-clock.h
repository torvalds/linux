/*
 * linux/arch/arm/mach-exynos/include/mach/exynos-clock.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * Header file for exynos4 clock support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_CLOCK_H
#define __ASM_ARCH_CLOCK_H __FILE__

#include <linux/clk.h>

extern struct clksrc_clk exynos4_clk_sclk_apll;
extern struct clksrc_clk exynos4_clk_mout_mpll;
extern struct clksrc_clk exynos4_clk_aclk_133;
extern struct clksrc_clk exynos4_clk_aclk_200;

extern struct clksrc_sources exynos4_clkset_aclk;
extern struct clk *exynos4_clkset_group_list[];
extern struct clk *exynos4_clkset_aclk_top_list[];

#endif /* __ASM_ARCH_CLOCK_H */

