/*
 * linux/arch/arm/mach-exynos4/include/mach/exynos4-clock.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
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

extern struct clk clk_sclk_hdmi27m;
extern struct clk clk_sclk_usbphy0;
extern struct clk clk_sclk_usbphy1;
extern struct clk clk_sclk_hdmiphy;

extern struct clksrc_clk clk_sclk_apll;
extern struct clksrc_clk clk_mout_mpll;
extern struct clksrc_clk clk_aclk_133;
extern struct clksrc_clk clk_mout_epll;
extern struct clksrc_clk clk_sclk_vpll;

extern struct clk *clkset_corebus_list[];
extern struct clksrc_sources clkset_mout_corebus;

extern struct clk *clkset_aclk_top_list[];
extern struct clksrc_sources clkset_aclk;

extern struct clk *clkset_group_list[];
extern struct clksrc_sources clkset_group;

extern int exynos4_clksrc_mask_fsys_ctrl(struct clk *clk, int enable);
extern int exynos4_clk_ip_fsys_ctrl(struct clk *clk, int enable);
extern int exynos4_clk_ip_lcd1_ctrl(struct clk *clk, int enable);

#endif /* __ASM_ARCH_CLOCK_H */
