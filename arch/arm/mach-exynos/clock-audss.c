/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Clock support for EXYNOS Audio Subsystem
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/err.h>

#include <plat/clock.h>
#include <plat/s5p-clock.h>
#include <plat/clock-clksrc.h>

#include <mach/map.h>
#include <mach/regs-audss.h>

static int exynos_clk_audss_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(EXYNOS_CLKGATE_AUDSS, clk, enable);
}

static struct clk *exynos_clkset_mout_audss_list[] = {
	&clk_ext_xtal_mux,
	&clk_fout_epll,
};

static struct clksrc_sources clkset_mout_audss = {
	.sources	= exynos_clkset_mout_audss_list,
	.nr_sources	= ARRAY_SIZE(exynos_clkset_mout_audss_list),
};

static struct clksrc_clk exynos_clk_mout_audss = {
	.clk	= {
		.name		= "mout_audss",
	},
	.sources = &clkset_mout_audss,
	.reg_src = { .reg = EXYNOS_CLKSRC_AUDSS, .shift = 0, .size = 1 },
};

static struct clk *exynos_clkset_mout_i2s_list[] = {
	&exynos_clk_mout_audss.clk,
};

static struct clksrc_sources clkset_mout_i2s = {
	.sources	= exynos_clkset_mout_i2s_list,
	.nr_sources	= ARRAY_SIZE(exynos_clkset_mout_i2s_list),
};

static struct clksrc_clk exynos_clk_mout_i2s = {
	.clk	= {
		.name		= "mout_i2s",
	},
	.sources = &clkset_mout_i2s,
	.reg_src = { .reg = EXYNOS_CLKSRC_AUDSS, .shift = 2, .size = 2 },
};

static struct clksrc_clk exynos_clk_dout_audss_srp = {
	.clk	= {
		.name		= "dout_srp",
		.parent		= &exynos_clk_mout_audss.clk,
	},
	.reg_div = { .reg = EXYNOS_CLKDIV_AUDSS, .shift = 0, .size = 4 },
};

static struct clksrc_clk exynos_clk_dout_audss_bus = {
	.clk	= {
		.name		= "dout_bus",
		.parent		= &exynos_clk_dout_audss_srp.clk,
	},
	.reg_div = { .reg = EXYNOS_CLKDIV_AUDSS, .shift = 4, .size = 4 },
};

static struct clksrc_clk exynos_clk_dout_audss_i2s = {
	.clk	= {
		.name		= "dout_i2s",
		.parent		= &exynos_clk_mout_i2s.clk,
	},
	.reg_div = { .reg = EXYNOS_CLKDIV_AUDSS, .shift = 8, .size = 4 },
};

/* Clock initialization code */
static struct clksrc_clk *exynos_audss_clks[] = {
	&exynos_clk_mout_audss,
	&exynos_clk_mout_i2s,
	&exynos_clk_dout_audss_srp,
	&exynos_clk_dout_audss_bus,
	&exynos_clk_dout_audss_i2s,
};

static struct clk exynos_init_audss_clocks[] = {
	{
		.name		= "srpclk",
		.parent		= &exynos_clk_dout_audss_srp.clk,
		.enable		= exynos_clk_audss_ctrl,
		.ctrlbit	= EXYNOS_AUDSS_CLKGATE_RP | EXYNOS_AUDSS_CLKGATE_UART
				| EXYNOS_AUDSS_CLKGATE_TIMER,
	}, {
		.name		= "iis",
		.devname	= "samsung-i2s.0",
		.parent		= &exynos_clk_dout_audss_i2s.clk,
		.enable		= exynos_clk_audss_ctrl,
		.ctrlbit	= EXYNOS_AUDSS_CLKGATE_I2SSPECIAL | EXYNOS_AUDSS_CLKGATE_I2SBUS
				| EXYNOS_AUDSS_CLKGATE_GPIO,
	}, {
		.name		= "iis",
		.devname	= "samsung-i2s.4",
		.parent		= &exynos_clk_dout_audss_i2s.clk,
		.enable		= exynos_clk_audss_ctrl,
		.ctrlbit	= EXYNOS_AUDSS_CLKGATE_I2SSPECIAL | EXYNOS_AUDSS_CLKGATE_I2SBUS
				| EXYNOS_AUDSS_CLKGATE_GPIO,
	}, {
		.name		= "pcm",
		.devname	= "samsung-pcm.0",
		.parent		= &exynos_clk_dout_audss_i2s.clk,
		.enable		= exynos_clk_audss_ctrl,
		.ctrlbit	= EXYNOS_AUDSS_CLKGATE_PCMSPECIAL | EXYNOS_AUDSS_CLKGATE_PCMBUS
				| EXYNOS_AUDSS_CLKGATE_GPIO,
	},
};

void __init exynos_register_audss_clocks(void)
{
	int ptr;

	for (ptr = 0; ptr < ARRAY_SIZE(exynos_audss_clks); ptr++)
		s3c_register_clksrc(exynos_audss_clks[ptr], 1);

	s3c_register_clocks(exynos_init_audss_clocks, ARRAY_SIZE(exynos_init_audss_clocks));
	s3c_disable_clocks(exynos_init_audss_clocks, ARRAY_SIZE(exynos_init_audss_clocks));
}
