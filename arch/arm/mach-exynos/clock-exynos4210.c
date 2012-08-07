/*
 * Copyright (c) 2011-2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4210 - Clock support
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
#include <mach/sysmmu.h>

#include "common.h"
#include "clock-exynos4.h"

#ifdef CONFIG_PM_SLEEP
static struct sleep_save exynos4210_clock_save[] = {
	SAVE_ITEM(EXYNOS4_CLKSRC_IMAGE),
	SAVE_ITEM(EXYNOS4_CLKDIV_IMAGE),
	SAVE_ITEM(EXYNOS4210_CLKSRC_LCD1),
	SAVE_ITEM(EXYNOS4210_CLKDIV_LCD1),
	SAVE_ITEM(EXYNOS4210_CLKSRC_MASK_LCD1),
	SAVE_ITEM(EXYNOS4210_CLKGATE_IP_IMAGE),
	SAVE_ITEM(EXYNOS4210_CLKGATE_IP_LCD1),
	SAVE_ITEM(EXYNOS4210_CLKGATE_IP_PERIR),
};
#endif

static struct clksrc_clk *sysclks[] = {
	/* nothing here yet */
};

static struct clksrc_clk exynos4210_clk_mout_g2d0 = {
	.clk	= {
		.name		= "mout_g2d0",
	},
	.sources = &exynos4_clkset_mout_g2d0,
	.reg_src = { .reg = EXYNOS4_CLKSRC_IMAGE, .shift = 0, .size = 1 },
};

static struct clksrc_clk exynos4210_clk_mout_g2d1 = {
	.clk	= {
		.name		= "mout_g2d1",
	},
	.sources = &exynos4_clkset_mout_g2d1,
	.reg_src = { .reg = EXYNOS4_CLKSRC_IMAGE, .shift = 4, .size = 1 },
};

static struct clk *exynos4210_clkset_mout_g2d_list[] = {
	[0] = &exynos4210_clk_mout_g2d0.clk,
	[1] = &exynos4210_clk_mout_g2d1.clk,
};

static struct clksrc_sources exynos4210_clkset_mout_g2d = {
	.sources	= exynos4210_clkset_mout_g2d_list,
	.nr_sources	= ARRAY_SIZE(exynos4210_clkset_mout_g2d_list),
};

static int exynos4_clksrc_mask_lcd1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS4210_CLKSRC_MASK_LCD1, clk, enable);
}

static struct clksrc_clk clksrcs[] = {
	{
		.clk		= {
			.name		= "sclk_sata",
			.id		= -1,
			.enable		= exynos4_clksrc_mask_fsys_ctrl,
			.ctrlbit	= (1 << 24),
		},
		.sources = &exynos4_clkset_mout_corebus,
		.reg_src = { .reg = EXYNOS4_CLKSRC_FSYS, .shift = 24, .size = 1 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_FSYS0, .shift = 20, .size = 4 },
	}, {
		.clk		= {
			.name		= "sclk_fimd",
			.devname	= "exynos4-fb.1",
			.enable		= exynos4_clksrc_mask_lcd1_ctrl,
			.ctrlbit	= (1 << 0),
		},
		.sources = &exynos4_clkset_group,
		.reg_src = { .reg = EXYNOS4210_CLKSRC_LCD1, .shift = 0, .size = 4 },
		.reg_div = { .reg = EXYNOS4210_CLKDIV_LCD1, .shift = 0, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_fimg2d",
		},
		.sources = &exynos4210_clkset_mout_g2d,
		.reg_src = { .reg = EXYNOS4_CLKSRC_IMAGE, .shift = 8, .size = 1 },
		.reg_div = { .reg = EXYNOS4_CLKDIV_IMAGE, .shift = 0, .size = 4 },
	},
};

static struct clk init_clocks_off[] = {
	{
		.name		= "sataphy",
		.id		= -1,
		.parent		= &exynos4_clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "sata",
		.id		= -1,
		.parent		= &exynos4_clk_aclk_133.clk,
		.enable		= exynos4_clk_ip_fsys_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "fimd",
		.devname	= "exynos4-fb.1",
		.enable		= exynos4_clk_ip_lcd1_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(2d, 14),
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= SYSMMU_CLOCK_NAME,
		.devname	= SYSMMU_CLOCK_DEVNAME(fimd1, 11),
		.enable		= exynos4_clk_ip_lcd1_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "fimg2d",
		.enable		= exynos4_clk_ip_image_ctrl,
		.ctrlbit	= (1 << 0),
	},
};

#ifdef CONFIG_PM_SLEEP
static int exynos4210_clock_suspend(void)
{
	s3c_pm_do_save(exynos4210_clock_save, ARRAY_SIZE(exynos4210_clock_save));

	return 0;
}

static void exynos4210_clock_resume(void)
{
	s3c_pm_do_restore_core(exynos4210_clock_save, ARRAY_SIZE(exynos4210_clock_save));
}

#else
#define exynos4210_clock_suspend NULL
#define exynos4210_clock_resume NULL
#endif

static struct syscore_ops exynos4210_clock_syscore_ops = {
	.suspend	= exynos4210_clock_suspend,
	.resume		= exynos4210_clock_resume,
};

void __init exynos4210_register_clocks(void)
{
	int ptr;

	exynos4_clk_mout_mpll.reg_src.reg = EXYNOS4_CLKSRC_CPU;
	exynos4_clk_mout_mpll.reg_src.shift = 8;
	exynos4_clk_mout_mpll.reg_src.size = 1;

	for (ptr = 0; ptr < ARRAY_SIZE(sysclks); ptr++)
		s3c_register_clksrc(sysclks[ptr], 1);

	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));

	s3c_register_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));
	s3c_disable_clocks(init_clocks_off, ARRAY_SIZE(init_clocks_off));

	register_syscore_ops(&exynos4210_clock_syscore_ops);
}
