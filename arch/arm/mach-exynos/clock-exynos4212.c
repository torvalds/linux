/*
 * Copyright (c) 2011-2012 Samsung Electronics Co., Ltd.
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
#include <linux/syscore_ops.h>

#include <plat/cpu-freq.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/pll.h>
#include <plat/s5p-clock.h>
#include <plat/clock-clksrc.h>
#include <plat/pm.h>

#include <mach/hardware.h>
#include <mach/map.h>
#include <mach/regs-clock.h>

#include "common.h"
#include "clock-exynos4.h"

#ifdef CONFIG_PM_SLEEP
static struct sleep_save exynos4212_clock_save[] = {
	SAVE_ITEM(EXYNOS4_CLKSRC_IMAGE),
	SAVE_ITEM(EXYNOS4_CLKDIV_IMAGE),
	SAVE_ITEM(EXYNOS4212_CLKGATE_IP_IMAGE),
	SAVE_ITEM(EXYNOS4212_CLKGATE_IP_PERIR),
};
#endif

static int exynos4212_clk_ip_isp0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_ISP0, clk, enable);
}

static int exynos4212_clk_ip_isp1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4_CLKGATE_IP_ISP1, clk, enable);
}

static struct clk *clk_src_mpll_user_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &exynos4_clk_mout_mpll.clk,
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
	.reg_src	= { .reg = EXYNOS4_CLKSRC_CPU, .shift = 24, .size = 1 },
};

static struct clksrc_clk exynos4x12_clk_mout_g2d0 = {
	.clk	= {
		.name		= "mout_g2d0",
	},
	.sources = &exynos4_clkset_mout_g2d0,
	.reg_src = { .reg = EXYNOS4_CLKSRC_DMC, .shift = 20, .size = 1 },
};

static struct clksrc_clk exynos4x12_clk_mout_g2d1 = {
	.clk	= {
		.name		= "mout_g2d1",
	},
	.sources = &exynos4_clkset_mout_g2d1,
	.reg_src = { .reg = EXYNOS4_CLKSRC_DMC, .shift = 24, .size = 1 },
};

static struct clk *exynos4x12_clkset_mout_g2d_list[] = {
	[0] = &exynos4x12_clk_mout_g2d0.clk,
	[1] = &exynos4x12_clk_mout_g2d1.clk,
};

static struct clksrc_sources exynos4x12_clkset_mout_g2d = {
	.sources	= exynos4x12_clkset_mout_g2d_list,
	.nr_sources	= ARRAY_SIZE(exynos4x12_clkset_mout_g2d_list),
};

static struct clksrc_clk *sysclks[] = {
	&clk_mout_mpll_user,
};

static struct clksrc_clk clksrcs[] = {
	{
		.clk	= {
			.name		= "sclk_fimg2d",
		},
		.sources = &exynos4x12_clkset_mout_g2d,
		.reg_src = { .reg = EXYNOS4_CLKSRC_DMC, .shift = 28, .size = 1 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_DMC1, .shift = 0, .size = 4 },
	},
};

static struct clk init_clocks_off[] = {
	{
		.name		= "sysmmu",
		.devname	= "exynos-sysmmu.9",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 24),
	}, {
		.name		= "sysmmu",
		.devname	= "exynos-sysmmu.12",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (7 << 8),
	}, {
		.name		= "sysmmu",
		.devname	= "exynos-sysmmu.13",
		.enable		= exynos4212_clk_ip_isp1_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "sysmmu",
		.devname	= "exynos-sysmmu.14",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= "sysmmu",
		.devname	= "exynos-sysmmu.15",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "flite",
		.devname	= "exynos-fimc-lite.0",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "flite",
		.devname	= "exynos-fimc-lite.1",
		.enable		= exynos4212_clk_ip_isp0_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "fimg2d",
		.enable		= exynos4_clk_ip_dmc_ctrl,
		.ctrlbit	= (1 << 23),
	},
};

#ifdef CONFIG_PM_SLEEP
static int exynos4212_clock_suspend(void)
{
	s3c_pm_do_save(exynos4212_clock_save, ARRAY_SIZE(exynos4212_clock_save));

	return 0;
}

static void exynos4212_clock_resume(void)
{
	s3c_pm_do_restore_core(exynos4212_clock_save, ARRAY_SIZE(exynos4212_clock_save));
}

#else
#define exynos4212_clock_suspend NULL
#define exynos4212_clock_resume NULL
#endif

static struct syscore_ops exynos4212_clock_syscore_ops = {
	.suspend	= exynos4212_clock_suspend,
	.resume		= exynos4212_clock_resume,
};

void __init exynos4212_register_clocks(void)
{
	int ptr;

	/* usbphy1 is removed */
	exynos4_clkset_group_list[4] = NULL;

	/* mout_mpll_user is used */
	exynos4_clkset_group_list[6] = &clk_mout_mpll_user.clk;
	exynos4_clkset_aclk_top_list[0] = &clk_mout_mpll_user.clk;

	exynos4_clk_mout_mpll.reg_src.reg = EXYNOS4_CLKSRC_DMC;
	exynos4_clk_mout_mpll.reg_src.shift = 12;
	exynos4_clk_mout_mpll.reg_src.size = 1;

	for (ptr = 0; ptr < ARRAY_SIZE(sysclks); ptr++)
		s3c_register_clksrc(sysclks[ptr], 1);

	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));

	s3c_register_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	s3c_disable_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));

	register_syscore_ops(&exynos4212_clock_syscore_ops);
}
