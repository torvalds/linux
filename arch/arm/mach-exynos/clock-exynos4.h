/*
 * Copyright (c) 2011-2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
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

extern struct clksrc_clk exynos4_clk_aclk_133;
extern struct clksrc_clk exynos4_clk_mout_mpll;

extern struct clksrc_sources exynos4_clkset_mout_corebus;
extern struct clksrc_sources exynos4_clkset_group;

extern struct clk *exynos4_clkset_aclk_top_list[];
extern struct clk *exynos4_clkset_group_list[];

extern struct clksrc_sources exynos4_clkset_mout_g2d0;
extern struct clksrc_sources exynos4_clkset_mout_g2d1;

extern int exynos4_clksrc_mask_fsys_ctrl(struct clk *clk, int enable);
extern int exynos4_clk_ip_fsys_ctrl(struct clk *clk, int enable);
extern int exynos4_clk_ip_lcd1_ctrl(struct clk *clk, int enable);
extern int exynos4_clk_ip_image_ctrl(struct clk *clk, int enable);
extern int exynos4_clk_ip_dmc_ctrl(struct clk *clk, int enable);

#endif /* __ASM_ARCH_CLOCK_H */
