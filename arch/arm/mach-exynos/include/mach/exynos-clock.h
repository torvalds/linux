/*
 * linux/arch/arm/mach-exynos/include/mach/exynos-clock.h
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

extern struct clk exynos4_clk_sclk_hdmi27m;
extern struct clk exynos4_clk_sclk_usbphy0;
extern struct clk exynos4_clk_sclk_usbphy1;
extern struct clk exynos4_clk_sclk_hdmiphy;
extern struct clk exynos4_clk_fimg2d;

extern struct clksrc_clk exynos4_clk_sclk_apll;
extern struct clksrc_clk exynos4_clk_mout_mpll;
extern struct clksrc_clk exynos4_clk_aclk_133;
extern struct clksrc_clk exynos4_clk_aclk_200;
#ifdef CONFIG_CPU_EXYNOS4212
extern struct clksrc_clk exynos4212_clk_aclk_266;
extern struct clksrc_clk exynos4212_clk_aclk_400_mcuisp;
#endif
extern struct clksrc_clk exynos4_clk_mout_epll;
extern struct clksrc_clk exynos4_clk_sclk_vpll;
extern struct clksrc_clk exynos4_clk_mout_g2d0;
extern struct clksrc_clk exynos4_clk_mout_g2d1;
extern struct clksrc_clk exynos4_clk_sclk_fimg2d;

extern struct clk *exynos4_clkset_corebus_list[];
extern struct clksrc_sources exynos4_clkset_mout_corebus;

extern struct clk *exynos4_clkset_aclk_top_list[];
extern struct clksrc_sources exynos4_clkset_aclk;

extern struct clk *exynos4_clkset_group_list[];
extern struct clksrc_sources exynos4_clkset_group;

extern struct clk *exynos4_clkset_mout_mfc0_list[];

extern struct clk exynos4_init_dmaclocks[];

/* For vpll  */
struct vpll_div_data {
	u32 rate;
	u32 pdiv;
	u32 mdiv;
	u32 sdiv;
	u32 k;
	u32 mfr;
	u32 mrr;
	u32 vsel;
};

extern struct clk_ops exynos4_vpll_ops;

extern int exynos4_clksrc_mask_fsys_ctrl(struct clk *clk, int enable);
extern int exynos4_clk_ip_fsys_ctrl(struct clk *clk, int enable);
extern int exynos4_clk_ip_lcd1_ctrl(struct clk *clk, int enable);
extern int exynos4_clk_ip_image_ctrl(struct clk *clk, int enable);
extern int exynos4_clk_ip_peril_ctrl(struct clk *clk, int enable);
extern int exynos4_clk_ip_dmc_ctrl(struct clk *clk, int enable);

#endif /* __ASM_ARCH_CLOCK_H */
