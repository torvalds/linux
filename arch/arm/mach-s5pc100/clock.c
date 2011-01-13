/* linux/arch/arm/mach-s5pc100/clock.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PC100 - Clock support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/map.h>

#include <plat/cpu-freq.h>
#include <mach/regs-clock.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/pll.h>
#include <plat/s5p-clock.h>
#include <plat/clock-clksrc.h>
#include <plat/s5pc100.h>

static struct clk s5p_clk_otgphy = {
	.name		= "otg_phy",
	.id		= -1,
};

static struct clk *clk_src_mout_href_list[] = {
	[0] = &s5p_clk_27m,
	[1] = &clk_fin_hpll,
};

static struct clksrc_sources clk_src_mout_href = {
	.sources	= clk_src_mout_href_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mout_href_list),
};

static struct clksrc_clk clk_mout_href = {
	.clk = {
		.name           = "mout_href",
		.id             = -1,
	},
	.sources        = &clk_src_mout_href,
	.reg_src        = { .reg = S5P_CLK_SRC0, .shift = 20, .size = 1 },
};

static struct clk *clk_src_mout_48m_list[] = {
	[0] = &clk_xusbxti,
	[1] = &s5p_clk_otgphy,
};

static struct clksrc_sources clk_src_mout_48m = {
	.sources	= clk_src_mout_48m_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mout_48m_list),
};

static struct clksrc_clk clk_mout_48m = {
	.clk = {
		.name           = "mout_48m",
		.id             = -1,
	},
	.sources        = &clk_src_mout_48m,
	.reg_src        = { .reg = S5P_CLK_SRC1, .shift = 24, .size = 1 },
};

static struct clksrc_clk clk_mout_mpll = {
	.clk = {
		.name           = "mout_mpll",
		.id             = -1,
	},
	.sources        = &clk_src_mpll,
	.reg_src        = { .reg = S5P_CLK_SRC0, .shift = 4, .size = 1 },
};


static struct clksrc_clk clk_mout_apll = {
	.clk    = {
		.name           = "mout_apll",
		.id             = -1,
	},
	.sources        = &clk_src_apll,
	.reg_src        = { .reg = S5P_CLK_SRC0, .shift = 0, .size = 1 },
};

static struct clksrc_clk clk_mout_epll = {
	.clk    = {
		.name           = "mout_epll",
		.id             = -1,
	},
	.sources        = &clk_src_epll,
	.reg_src        = { .reg = S5P_CLK_SRC0, .shift = 8, .size = 1 },
};

static struct clk *clk_src_mout_hpll_list[] = {
	[0] = &s5p_clk_27m,
};

static struct clksrc_sources clk_src_mout_hpll = {
	.sources	= clk_src_mout_hpll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mout_hpll_list),
};

static struct clksrc_clk clk_mout_hpll = {
	.clk    = {
		.name           = "mout_hpll",
		.id             = -1,
	},
	.sources        = &clk_src_mout_hpll,
	.reg_src        = { .reg = S5P_CLK_SRC0, .shift = 12, .size = 1 },
};

static struct clksrc_clk clk_div_apll = {
	.clk	= {
		.name	= "div_apll",
		.id	= -1,
		.parent	= &clk_mout_apll.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV0, .shift = 0, .size = 1 },
};

static struct clksrc_clk clk_div_arm = {
	.clk	= {
		.name	= "div_arm",
		.id	= -1,
		.parent	= &clk_div_apll.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV0, .shift = 4, .size = 3 },
};

static struct clksrc_clk clk_div_d0_bus = {
	.clk	= {
		.name	= "div_d0_bus",
		.id	= -1,
		.parent	= &clk_div_arm.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV0, .shift = 8, .size = 3 },
};

static struct clksrc_clk clk_div_pclkd0 = {
	.clk	= {
		.name	= "div_pclkd0",
		.id	= -1,
		.parent	= &clk_div_d0_bus.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV0, .shift = 12, .size = 3 },
};

static struct clksrc_clk clk_div_secss = {
	.clk	= {
		.name	= "div_secss",
		.id	= -1,
		.parent	= &clk_div_d0_bus.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV0, .shift = 16, .size = 3 },
};

static struct clksrc_clk clk_div_apll2 = {
	.clk	= {
		.name	= "div_apll2",
		.id	= -1,
		.parent	= &clk_mout_apll.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV1, .shift = 0, .size = 3 },
};

static struct clk *clk_src_mout_am_list[] = {
	[0] = &clk_mout_mpll.clk,
	[1] = &clk_div_apll2.clk,
};

struct clksrc_sources clk_src_mout_am = {
	.sources	= clk_src_mout_am_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mout_am_list),
};

static struct clksrc_clk clk_mout_am = {
	.clk	= {
		.name	= "mout_am",
		.id	= -1,
	},
	.sources = &clk_src_mout_am,
	.reg_src = { .reg = S5P_CLK_SRC0, .shift = 16, .size = 1 },
};

static struct clksrc_clk clk_div_d1_bus = {
	.clk	= {
		.name	= "div_d1_bus",
		.id	= -1,
		.parent	= &clk_mout_am.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV1, .shift = 12, .size = 3 },
};

static struct clksrc_clk clk_div_mpll2 = {
	.clk	= {
		.name	= "div_mpll2",
		.id	= -1,
		.parent	= &clk_mout_am.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV1, .shift = 8, .size = 1 },
};

static struct clksrc_clk clk_div_mpll = {
	.clk	= {
		.name	= "div_mpll",
		.id	= -1,
		.parent	= &clk_mout_am.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV1, .shift = 4, .size = 2 },
};

static struct clk *clk_src_mout_onenand_list[] = {
	[0] = &clk_div_d0_bus.clk,
	[1] = &clk_div_d1_bus.clk,
};

struct clksrc_sources clk_src_mout_onenand = {
	.sources	= clk_src_mout_onenand_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mout_onenand_list),
};

static struct clksrc_clk clk_mout_onenand = {
	.clk	= {
		.name	= "mout_onenand",
		.id	= -1,
	},
	.sources = &clk_src_mout_onenand,
	.reg_src = { .reg = S5P_CLK_SRC0, .shift = 24, .size = 1 },
};

static struct clksrc_clk clk_div_onenand = {
	.clk	= {
		.name	= "div_onenand",
		.id	= -1,
		.parent	= &clk_mout_onenand.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV1, .shift = 20, .size = 2 },
};

static struct clksrc_clk clk_div_pclkd1 = {
	.clk	= {
		.name	= "div_pclkd1",
		.id	= -1,
		.parent	= &clk_div_d1_bus.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV1, .shift = 16, .size = 3 },
};

static struct clksrc_clk clk_div_cam = {
	.clk	= {
		.name	= "div_cam",
		.id	= -1,
		.parent	= &clk_div_mpll2.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV1, .shift = 24, .size = 5 },
};

static struct clksrc_clk clk_div_hdmi = {
	.clk	= {
		.name	= "div_hdmi",
		.id	= -1,
		.parent	= &clk_mout_hpll.clk,
	},
	.reg_div = { .reg = S5P_CLK_DIV3, .shift = 28, .size = 4 },
};

static u32 epll_div[][4] = {
	{ 32750000,	131, 3, 4 },
	{ 32768000,	131, 3, 4 },
	{ 36000000,	72,  3, 3 },
	{ 45000000,	90,  3, 3 },
	{ 45158000,	90,  3, 3 },
	{ 45158400,	90,  3, 3 },
	{ 48000000,	96,  3, 3 },
	{ 49125000,	131, 4, 3 },
	{ 49152000,	131, 4, 3 },
	{ 60000000,	120, 3, 3 },
	{ 67737600,	226, 5, 3 },
	{ 67738000,	226, 5, 3 },
	{ 73800000,	246, 5, 3 },
	{ 73728000,	246, 5, 3 },
	{ 72000000,	144, 3, 3 },
	{ 84000000,	168, 3, 3 },
	{ 96000000,	96,  3, 2 },
	{ 144000000,	144, 3, 2 },
	{ 192000000,	96,  3, 1 }
};

static int s5pc100_epll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned int epll_con;
	unsigned int i;

	if (clk->rate == rate)	/* Return if nothing changed */
		return 0;

	epll_con = __raw_readl(S5P_EPLL_CON);

	epll_con &= ~(PLL65XX_MDIV_MASK | PLL65XX_PDIV_MASK | PLL65XX_SDIV_MASK);

	for (i = 0; i < ARRAY_SIZE(epll_div); i++) {
		if (epll_div[i][0] == rate) {
			epll_con |= (epll_div[i][1] << PLL65XX_MDIV_SHIFT) |
				    (epll_div[i][2] << PLL65XX_PDIV_SHIFT) |
				    (epll_div[i][3] << PLL65XX_SDIV_SHIFT);
			break;
		}
	}

	if (i == ARRAY_SIZE(epll_div)) {
		printk(KERN_ERR "%s: Invalid Clock EPLL Frequency\n", __func__);
		return -EINVAL;
	}

	__raw_writel(epll_con, S5P_EPLL_CON);

	printk(KERN_WARNING "EPLL Rate changes from %lu to %lu\n",
			clk->rate, rate);

	clk->rate = rate;

	return 0;
}

static struct clk_ops s5pc100_epll_ops = {
	.get_rate = s5p_epll_get_rate,
	.set_rate = s5pc100_epll_set_rate,
};

static int s5pc100_d0_0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_D00, clk, enable);
}

static int s5pc100_d0_1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_D01, clk, enable);
}

static int s5pc100_d0_2_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_D02, clk, enable);
}

static int s5pc100_d1_0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_D10, clk, enable);
}

static int s5pc100_d1_1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_D11, clk, enable);
}

static int s5pc100_d1_2_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_D12, clk, enable);
}

static int s5pc100_d1_3_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_D13, clk, enable);
}

static int s5pc100_d1_4_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_D14, clk, enable);
}

static int s5pc100_d1_5_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_D15, clk, enable);
}

static int s5pc100_sclk0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_SCLK0, clk, enable);
}

static int s5pc100_sclk1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_SCLK1, clk, enable);
}

/*
 * The following clocks will be disabled during clock initialization. It is
 * recommended to keep the following clocks disabled until the driver requests
 * for enabling the clock.
 */
static struct clk init_clocks_disable[] = {
	{
		.name		= "cssys",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_0_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "secss",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_0_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "g2d",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_0_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "mdma",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_0_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "cfcon",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_0_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "nfcon",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_1_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "onenandc",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_1_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "sdm",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_2_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "seckey",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_2_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "hsmmc",
		.id		= 2,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_0_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "hsmmc",
		.id		= 1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_0_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "hsmmc",
		.id		= 0,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_0_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "modemif",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_0_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "otg",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_0_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "usbhost",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_0_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "pdma",
		.id		= 1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_0_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "pdma",
		.id		= 0,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_0_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "lcd",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_1_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "rotator",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_1_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "fimc",
		.id		= 0,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_1_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "fimc",
		.id		= 1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_1_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "fimc",
		.id		= 2,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_1_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "jpeg",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_1_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "mipi-dsim",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_1_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "mipi-csis",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_1_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "g3d",
		.id		= 0,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_0_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "tv",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_2_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "vp",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_2_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "mixer",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_2_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "hdmi",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_2_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "mfc",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_2_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "apc",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_3_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "iec",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_3_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "systimer",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_3_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "watchdog",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_3_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "rtc",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_3_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "i2c",
		.id		= 0,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "i2c",
		.id		= 1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "spi",
		.id		= 0,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "spi",
		.id		= 1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "spi",
		.id		= 2,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "irda",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "ccan",
		.id		= 0,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 10),
	}, {
		.name		= "ccan",
		.id		= 1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 11),
	}, {
		.name		= "hsitx",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 12),
	}, {
		.name		= "hsirx",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 13),
	}, {
		.name		= "iis",
		.id		= 0,
		.parent		= &clk_div_pclkd1.clk,
		.enable		= s5pc100_d1_5_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "iis",
		.id		= 1,
		.parent		= &clk_div_pclkd1.clk,
		.enable		= s5pc100_d1_5_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "iis",
		.id		= 2,
		.parent		= &clk_div_pclkd1.clk,
		.enable		= s5pc100_d1_5_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "ac97",
		.id		= -1,
		.parent		= &clk_div_pclkd1.clk,
		.enable		= s5pc100_d1_5_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "pcm",
		.id		= 0,
		.parent		= &clk_div_pclkd1.clk,
		.enable		= s5pc100_d1_5_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "pcm",
		.id		= 1,
		.parent		= &clk_div_pclkd1.clk,
		.enable		= s5pc100_d1_5_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "spdif",
		.id		= -1,
		.parent		= &clk_div_pclkd1.clk,
		.enable		= s5pc100_d1_5_ctrl,
		.ctrlbit	= (1 << 6),
	}, {
		.name		= "adc",
		.id		= -1,
		.parent		= &clk_div_pclkd1.clk,
		.enable		= s5pc100_d1_5_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "keypad",
		.id		= -1,
		.parent		= &clk_div_pclkd1.clk,
		.enable		= s5pc100_d1_5_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "spi_48m",
		.id		= 0,
		.parent		= &clk_mout_48m.clk,
		.enable		= s5pc100_sclk0_ctrl,
		.ctrlbit	= (1 << 7),
	}, {
		.name		= "spi_48m",
		.id		= 1,
		.parent		= &clk_mout_48m.clk,
		.enable		= s5pc100_sclk0_ctrl,
		.ctrlbit	= (1 << 8),
	}, {
		.name		= "spi_48m",
		.id		= 2,
		.parent		= &clk_mout_48m.clk,
		.enable		= s5pc100_sclk0_ctrl,
		.ctrlbit	= (1 << 9),
	}, {
		.name		= "mmc_48m",
		.id		= 0,
		.parent		= &clk_mout_48m.clk,
		.enable		= s5pc100_sclk0_ctrl,
		.ctrlbit	= (1 << 15),
	}, {
		.name		= "mmc_48m",
		.id		= 1,
		.parent		= &clk_mout_48m.clk,
		.enable		= s5pc100_sclk0_ctrl,
		.ctrlbit	= (1 << 16),
	}, {
		.name		= "mmc_48m",
		.id		= 2,
		.parent		= &clk_mout_48m.clk,
		.enable		= s5pc100_sclk0_ctrl,
		.ctrlbit	= (1 << 17),
	},
};

static struct clk clk_vclk54m = {
	.name		= "vclk_54m",
	.id		= -1,
	.rate		= 54000000,
};

static struct clk clk_i2scdclk0 = {
	.name		= "i2s_cdclk0",
	.id		= -1,
};

static struct clk clk_i2scdclk1 = {
	.name		= "i2s_cdclk1",
	.id		= -1,
};

static struct clk clk_i2scdclk2 = {
	.name		= "i2s_cdclk2",
	.id		= -1,
};

static struct clk clk_pcmcdclk0 = {
	.name		= "pcm_cdclk0",
	.id		= -1,
};

static struct clk clk_pcmcdclk1 = {
	.name		= "pcm_cdclk1",
	.id		= -1,
};

static struct clk *clk_src_group1_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_div_mpll2.clk,
	[2] = &clk_fin_epll,
	[3] = &clk_mout_hpll.clk,
};

struct clksrc_sources clk_src_group1 = {
	.sources	= clk_src_group1_list,
	.nr_sources	= ARRAY_SIZE(clk_src_group1_list),
};

static struct clk *clk_src_group2_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_div_mpll.clk,
};

struct clksrc_sources clk_src_group2 = {
	.sources	= clk_src_group2_list,
	.nr_sources	= ARRAY_SIZE(clk_src_group2_list),
};

static struct clk *clk_src_group3_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_div_mpll.clk,
	[2] = &clk_fin_epll,
	[3] = &clk_i2scdclk0,
	[4] = &clk_pcmcdclk0,
	[5] = &clk_mout_hpll.clk,
};

struct clksrc_sources clk_src_group3 = {
	.sources	= clk_src_group3_list,
	.nr_sources	= ARRAY_SIZE(clk_src_group3_list),
};

static struct clksrc_clk clk_sclk_audio0 = {
	.clk	= {
		.name		= "sclk_audio",
		.id		= 0,
		.ctrlbit	= (1 << 8),
		.enable		= s5pc100_sclk1_ctrl,
	},
	.sources = &clk_src_group3,
	.reg_src = { .reg = S5P_CLK_SRC3, .shift = 12, .size = 3 },
	.reg_div = { .reg = S5P_CLK_DIV4, .shift = 12, .size = 4 },
};

static struct clk *clk_src_group4_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_div_mpll.clk,
	[2] = &clk_fin_epll,
	[3] = &clk_i2scdclk1,
	[4] = &clk_pcmcdclk1,
	[5] = &clk_mout_hpll.clk,
};

struct clksrc_sources clk_src_group4 = {
	.sources	= clk_src_group4_list,
	.nr_sources	= ARRAY_SIZE(clk_src_group4_list),
};

static struct clksrc_clk clk_sclk_audio1 = {
	.clk	= {
		.name		= "sclk_audio",
		.id		= 1,
		.ctrlbit	= (1 << 9),
		.enable		= s5pc100_sclk1_ctrl,
	},
	.sources = &clk_src_group4,
	.reg_src = { .reg = S5P_CLK_SRC3, .shift = 16, .size = 3 },
	.reg_div = { .reg = S5P_CLK_DIV4, .shift = 16, .size = 4 },
};

static struct clk *clk_src_group5_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_div_mpll.clk,
	[2] = &clk_fin_epll,
	[3] = &clk_i2scdclk2,
	[4] = &clk_mout_hpll.clk,
};

struct clksrc_sources clk_src_group5 = {
	.sources	= clk_src_group5_list,
	.nr_sources	= ARRAY_SIZE(clk_src_group5_list),
};

static struct clksrc_clk clk_sclk_audio2 = {
	.clk	= {
		.name		= "sclk_audio",
		.id		= 2,
		.ctrlbit	= (1 << 10),
		.enable		= s5pc100_sclk1_ctrl,
	},
	.sources = &clk_src_group5,
	.reg_src = { .reg = S5P_CLK_SRC3, .shift = 20, .size = 3 },
	.reg_div = { .reg = S5P_CLK_DIV4, .shift = 20, .size = 4 },
};

static struct clk *clk_src_group6_list[] = {
	[0] = &s5p_clk_27m,
	[1] = &clk_vclk54m,
	[2] = &clk_div_hdmi.clk,
};

struct clksrc_sources clk_src_group6 = {
	.sources	= clk_src_group6_list,
	.nr_sources	= ARRAY_SIZE(clk_src_group6_list),
};

static struct clk *clk_src_group7_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_div_mpll.clk,
	[2] = &clk_mout_hpll.clk,
	[3] = &clk_vclk54m,
};

struct clksrc_sources clk_src_group7 = {
	.sources	= clk_src_group7_list,
	.nr_sources	= ARRAY_SIZE(clk_src_group7_list),
};

static struct clk *clk_src_mmc0_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_div_mpll.clk,
	[2] = &clk_fin_epll,
};

struct clksrc_sources clk_src_mmc0 = {
	.sources	= clk_src_mmc0_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mmc0_list),
};

static struct clk *clk_src_mmc12_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_div_mpll.clk,
	[2] = &clk_fin_epll,
	[3] = &clk_mout_hpll.clk,
};

struct clksrc_sources clk_src_mmc12 = {
	.sources	= clk_src_mmc12_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mmc12_list),
};

static struct clk *clk_src_irda_usb_list[] = {
	[0] = &clk_mout_epll.clk,
	[1] = &clk_div_mpll.clk,
	[2] = &clk_fin_epll,
	[3] = &clk_mout_hpll.clk,
};

struct clksrc_sources clk_src_irda_usb = {
	.sources	= clk_src_irda_usb_list,
	.nr_sources	= ARRAY_SIZE(clk_src_irda_usb_list),
};

static struct clk *clk_src_pwi_list[] = {
	[0] = &clk_fin_epll,
	[1] = &clk_mout_epll.clk,
	[2] = &clk_div_mpll.clk,
};

struct clksrc_sources clk_src_pwi = {
	.sources	= clk_src_pwi_list,
	.nr_sources	= ARRAY_SIZE(clk_src_pwi_list),
};

static struct clk *clk_sclk_spdif_list[] = {
	[0] = &clk_sclk_audio0.clk,
	[1] = &clk_sclk_audio1.clk,
	[2] = &clk_sclk_audio2.clk,
};

struct clksrc_sources clk_src_sclk_spdif = {
	.sources	= clk_sclk_spdif_list,
	.nr_sources	= ARRAY_SIZE(clk_sclk_spdif_list),
};

static int s5pc100_spdif_set_rate(struct clk *clk, unsigned long rate)
{
	struct clk *pclk;
	int ret;

	pclk = clk_get_parent(clk);
	if (IS_ERR(pclk))
		return -EINVAL;

	ret = pclk->ops->set_rate(pclk, rate);
	clk_put(pclk);

	return ret;
}

static unsigned long s5pc100_spdif_get_rate(struct clk *clk)
{
	struct clk *pclk;
	int rate;

	pclk = clk_get_parent(clk);
	if (IS_ERR(pclk))
		return -EINVAL;

	rate = pclk->ops->get_rate(clk);
	clk_put(pclk);

	return rate;
}

static struct clk_ops s5pc100_sclk_spdif_ops = {
	.set_rate	= s5pc100_spdif_set_rate,
	.get_rate	= s5pc100_spdif_get_rate,
};

static struct clksrc_clk clk_sclk_spdif = {
	.clk	= {
		.name		= "sclk_spdif",
		.id		= -1,
		.ctrlbit	= (1 << 11),
		.enable		= s5pc100_sclk1_ctrl,
		.ops		= &s5pc100_sclk_spdif_ops,
	},
	.sources = &clk_src_sclk_spdif,
	.reg_src = { .reg = S5P_CLK_SRC3, .shift = 24, .size = 2 },
};

static struct clksrc_clk clksrcs[] = {
	{
		.clk	= {
			.name		= "sclk_spi",
			.id		= 0,
			.ctrlbit	= (1 << 4),
			.enable		= s5pc100_sclk0_ctrl,

		},
		.sources = &clk_src_group1,
		.reg_src = { .reg = S5P_CLK_SRC1, .shift = 4, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV2, .shift = 4, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_spi",
			.id		= 1,
			.ctrlbit	= (1 << 5),
			.enable		= s5pc100_sclk0_ctrl,

		},
		.sources = &clk_src_group1,
		.reg_src = { .reg = S5P_CLK_SRC1, .shift = 8, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV2, .shift = 8, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_spi",
			.id		= 2,
			.ctrlbit	= (1 << 6),
			.enable		= s5pc100_sclk0_ctrl,

		},
		.sources = &clk_src_group1,
		.reg_src = { .reg = S5P_CLK_SRC1, .shift = 12, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV2, .shift = 12, .size = 4 },
	}, {
		.clk	= {
			.name		= "uclk1",
			.id		= -1,
			.ctrlbit	= (1 << 3),
			.enable		= s5pc100_sclk0_ctrl,

		},
		.sources = &clk_src_group2,
		.reg_src = { .reg = S5P_CLK_SRC1, .shift = 0, .size = 1 },
		.reg_div = { .reg = S5P_CLK_DIV2, .shift = 0, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_mixer",
			.id		= -1,
			.ctrlbit	= (1 << 6),
			.enable		= s5pc100_sclk0_ctrl,

		},
		.sources = &clk_src_group6,
		.reg_src = { .reg = S5P_CLK_SRC2, .shift = 28, .size = 2 },
	}, {
		.clk	= {
			.name		= "sclk_lcd",
			.id		= -1,
			.ctrlbit	= (1 << 0),
			.enable		= s5pc100_sclk1_ctrl,

		},
		.sources = &clk_src_group7,
		.reg_src = { .reg = S5P_CLK_SRC2, .shift = 12, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV3, .shift = 12, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_fimc",
			.id		= 0,
			.ctrlbit	= (1 << 1),
			.enable		= s5pc100_sclk1_ctrl,

		},
		.sources = &clk_src_group7,
		.reg_src = { .reg = S5P_CLK_SRC2, .shift = 16, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV3, .shift = 16, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_fimc",
			.id		= 1,
			.ctrlbit	= (1 << 2),
			.enable		= s5pc100_sclk1_ctrl,

		},
		.sources = &clk_src_group7,
		.reg_src = { .reg = S5P_CLK_SRC2, .shift = 20, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV3, .shift = 20, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_fimc",
			.id		= 2,
			.ctrlbit	= (1 << 3),
			.enable		= s5pc100_sclk1_ctrl,

		},
		.sources = &clk_src_group7,
		.reg_src = { .reg = S5P_CLK_SRC2, .shift = 24, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV3, .shift = 24, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_mmc",
			.id		= 0,
			.ctrlbit	= (1 << 12),
			.enable		= s5pc100_sclk1_ctrl,

		},
		.sources = &clk_src_mmc0,
		.reg_src = { .reg = S5P_CLK_SRC2, .shift = 0, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV3, .shift = 0, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_mmc",
			.id		= 1,
			.ctrlbit	= (1 << 13),
			.enable		= s5pc100_sclk1_ctrl,

		},
		.sources = &clk_src_mmc12,
		.reg_src = { .reg = S5P_CLK_SRC2, .shift = 4, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV3, .shift = 4, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_mmc",
			.id		= 2,
			.ctrlbit	= (1 << 14),
			.enable		= s5pc100_sclk1_ctrl,

		},
		.sources = &clk_src_mmc12,
		.reg_src = { .reg = S5P_CLK_SRC2, .shift = 8, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV3, .shift = 8, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_irda",
			.id		= 2,
			.ctrlbit	= (1 << 10),
			.enable		= s5pc100_sclk0_ctrl,

		},
		.sources = &clk_src_irda_usb,
		.reg_src = { .reg = S5P_CLK_SRC2, .shift = 8, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV3, .shift = 8, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_irda",
			.id		= -1,
			.ctrlbit	= (1 << 10),
			.enable		= s5pc100_sclk0_ctrl,

		},
		.sources = &clk_src_mmc12,
		.reg_src = { .reg = S5P_CLK_SRC1, .shift = 16, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV2, .shift = 16, .size = 4 },
	}, {
		.clk	= {
			.name		= "sclk_pwi",
			.id		= -1,
			.ctrlbit	= (1 << 1),
			.enable		= s5pc100_sclk0_ctrl,

		},
		.sources = &clk_src_pwi,
		.reg_src = { .reg = S5P_CLK_SRC3, .shift = 0, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV4, .shift = 0, .size = 3 },
	}, {
		.clk	= {
			.name		= "sclk_uhost",
			.id		= -1,
			.ctrlbit	= (1 << 11),
			.enable		= s5pc100_sclk0_ctrl,

		},
		.sources = &clk_src_irda_usb,
		.reg_src = { .reg = S5P_CLK_SRC1, .shift = 20, .size = 2 },
		.reg_div = { .reg = S5P_CLK_DIV2, .shift = 20, .size = 4 },
	},
};

/* Clock initialisation code */
static struct clksrc_clk *sysclks[] = {
	&clk_mout_apll,
	&clk_mout_epll,
	&clk_mout_mpll,
	&clk_mout_hpll,
	&clk_mout_href,
	&clk_mout_48m,
	&clk_div_apll,
	&clk_div_arm,
	&clk_div_d0_bus,
	&clk_div_pclkd0,
	&clk_div_secss,
	&clk_div_apll2,
	&clk_mout_am,
	&clk_div_d1_bus,
	&clk_div_mpll2,
	&clk_div_mpll,
	&clk_mout_onenand,
	&clk_div_onenand,
	&clk_div_pclkd1,
	&clk_div_cam,
	&clk_div_hdmi,
	&clk_sclk_audio0,
	&clk_sclk_audio1,
	&clk_sclk_audio2,
	&clk_sclk_spdif,
};

void __init_or_cpufreq s5pc100_setup_clocks(void)
{
	unsigned long xtal;
	unsigned long arm;
	unsigned long hclkd0;
	unsigned long hclkd1;
	unsigned long pclkd0;
	unsigned long pclkd1;
	unsigned long apll;
	unsigned long mpll;
	unsigned long epll;
	unsigned long hpll;
	unsigned int ptr;

	/* Set S5PC100 functions for clk_fout_epll */
	clk_fout_epll.enable = s5p_epll_enable;
	clk_fout_epll.ops = &s5pc100_epll_ops;

	printk(KERN_DEBUG "%s: registering clocks\n", __func__);

	xtal = clk_get_rate(&clk_xtal);

	printk(KERN_DEBUG "%s: xtal is %ld\n", __func__, xtal);

	apll = s5p_get_pll65xx(xtal, __raw_readl(S5P_APLL_CON));
	mpll = s5p_get_pll65xx(xtal, __raw_readl(S5P_MPLL_CON));
	epll = s5p_get_pll65xx(xtal, __raw_readl(S5P_EPLL_CON));
	hpll = s5p_get_pll65xx(xtal, __raw_readl(S5P_HPLL_CON));

	printk(KERN_INFO "S5PC100: PLL settings, A=%ld.%ldMHz, M=%ld.%ldMHz, E=%ld.%ldMHz, H=%ld.%ldMHz\n",
			print_mhz(apll), print_mhz(mpll), print_mhz(epll), print_mhz(hpll));

	clk_fout_apll.rate = apll;
	clk_fout_mpll.rate = mpll;
	clk_fout_epll.rate = epll;
	clk_mout_hpll.clk.rate = hpll;

	for (ptr = 0; ptr < ARRAY_SIZE(clksrcs); ptr++)
		s3c_set_clksrc(&clksrcs[ptr], true);

	arm = clk_get_rate(&clk_div_arm.clk);
	hclkd0 = clk_get_rate(&clk_div_d0_bus.clk);
	pclkd0 = clk_get_rate(&clk_div_pclkd0.clk);
	hclkd1 = clk_get_rate(&clk_div_d1_bus.clk);
	pclkd1 = clk_get_rate(&clk_div_pclkd1.clk);

	printk(KERN_INFO "S5PC100: HCLKD0=%ld.%ldMHz, HCLKD1=%ld.%ldMHz, PCLKD0=%ld.%ldMHz, PCLKD1=%ld.%ldMHz\n",
			print_mhz(hclkd0), print_mhz(hclkd1), print_mhz(pclkd0), print_mhz(pclkd1));

	clk_f.rate = arm;
	clk_h.rate = hclkd1;
	clk_p.rate = pclkd1;
}

/*
 * The following clocks will be enabled during clock initialization.
 */
static struct clk init_clocks[] = {
	{
		.name		= "tzic",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_0_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "intc",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_0_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "ebi",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_1_ctrl,
		.ctrlbit	= (1 << 5),
	}, {
		.name		= "intmem",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_1_ctrl,
		.ctrlbit	= (1 << 4),
	}, {
		.name		= "sromc",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_1_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "dmc",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_1_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "chipid",
		.id		= -1,
		.parent		= &clk_div_d0_bus.clk,
		.enable		= s5pc100_d0_1_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "gpio",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_3_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "uart",
		.id		= 0,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 0),
	}, {
		.name		= "uart",
		.id		= 1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 1),
	}, {
		.name		= "uart",
		.id		= 2,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 2),
	}, {
		.name		= "uart",
		.id		= 3,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_4_ctrl,
		.ctrlbit	= (1 << 3),
	}, {
		.name		= "timers",
		.id		= -1,
		.parent		= &clk_div_d1_bus.clk,
		.enable		= s5pc100_d1_3_ctrl,
		.ctrlbit	= (1 << 6),
	},
};

static struct clk *clks[] __initdata = {
	&clk_ext,
	&clk_i2scdclk0,
	&clk_i2scdclk1,
	&clk_i2scdclk2,
	&clk_pcmcdclk0,
	&clk_pcmcdclk1,
};

void __init s5pc100_register_clocks(void)
{
	struct clk *clkp;
	int ret;
	int ptr;

	s3c24xx_register_clocks(clks, ARRAY_SIZE(clks));

	for (ptr = 0; ptr < ARRAY_SIZE(sysclks); ptr++)
		s3c_register_clksrc(sysclks[ptr], 1);

	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));
	s3c_register_clocks(init_clocks, ARRAY_SIZE(init_clocks));

	clkp = init_clocks_disable;
	for (ptr = 0; ptr < ARRAY_SIZE(init_clocks_disable); ptr++, clkp++) {

		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
		(clkp->enable)(clkp, 0);
	}

	s3c_pwmclk_init();
}
