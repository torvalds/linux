/*
 * linux/arch/arm/mach-exynos4/clock-exynos4212.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4212 - Clock support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <plat/cpu-freq.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/pll.h>
#include <plat/s5p-clock.h>
#include <plat/clock-clksrc.h>
#include <plat/exynos4.h>

#include <mach/hardware.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/exynos4-clock.h>

static struct clk *clk_src_mpll_user_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &clk_mout_mpll.clk,
};

static struct clksrc_sources clk_src_mpll_user = {
	.sources	= clk_src_mpll_user_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mpll_user_list),
};

static struct clksrc_clk clk_mout_mpll_user = {
	.clk = {
		.name		= "mout_mpll_user",
	},
	.sources	= &clk_src_mpll_user,
	.reg_src	= { .reg = S5P_CLKSRC_CPU, .shift = 24, .size = 1 },
};

static struct clksrc_clk *sysclks[] = {
	&clk_mout_mpll_user,
};

static struct clksrc_clk clksrcs[] = {
	/* nothing here yet */
};

static struct clk init_clocks_off[] = {
	/* nothing here yet */
};

void __init exynos4212_register_clocks(void)
{
	int ptr;

	/* usbphy1 is removed */
	clkset_group_list[4] = NULL;

	/* mout_mpll_user is used */
	clkset_group_list[6] = &clk_mout_mpll_user.clk;
	clkset_aclk_top_list[0] = &clk_mout_mpll_user.clk;

	clk_mout_mpll.reg_src.reg = S5P_CLKSRC_DMC;
	clk_mout_mpll.reg_src.shift = 12;
	clk_mout_mpll.reg_src.size = 1;

	for (ptr = 0; ptr < ARRAY_SIZE(sysclks); ptr++)
		s3c_register_clksrc(sysclks[ptr], 1);

	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));

	s3c_register_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	s3c_disable_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
}
