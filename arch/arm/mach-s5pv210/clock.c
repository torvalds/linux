/* linux/arch/arm/mach-s5pv210/clock.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV210 - Clock support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/sysdev.h>
#include <linux/io.h>

#include <mach/map.h>

#include <plat/cpu-freq.h>
#include <mach/regs-clock.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/pll.h>
#include <plat/s5p-clock.h>
#include <plat/clock-clksrc.h>
#include <plat/s5pv210.h>

static int s5pv210_clk_ip0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP0, clk, enable);
}

static int s5pv210_clk_ip1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP1, clk, enable);
}

static int s5pv210_clk_ip2_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP2, clk, enable);
}

static int s5pv210_clk_ip3_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P_CLKGATE_IP3, clk, enable);
}

static struct clk clk_h200 = {
	.name		= "hclk200",
	.id		= -1,
};

static struct clk clk_h100 = {
	.name		= "hclk100",
	.id		= -1,
};

static struct clk clk_h166 = {
	.name		= "hclk166",
	.id		= -1,
};

static struct clk clk_h133 = {
	.name		= "hclk133",
	.id		= -1,
};

static struct clk clk_p100 = {
	.name		= "pclk100",
	.id		= -1,
};

static struct clk clk_p83 = {
	.name		= "pclk83",
	.id		= -1,
};

static struct clk clk_p66 = {
	.name		= "pclk66",
	.id		= -1,
};

static struct clk *sys_clks[] = {
	&clk_h200,
	&clk_h100,
	&clk_h166,
	&clk_h133,
	&clk_p100,
	&clk_p83,
	&clk_p66
};

static struct clk init_clocks_disable[] = {
	{
		.name		= "rot",
		.id		= -1,
		.parent		= &clk_h166,
		.enable		= s5pv210_clk_ip0_ctrl,
		.ctrlbit	= (1<<29),
	}, {
		.name		= "otg",
		.id		= -1,
		.parent		= &clk_h133,
		.enable		= s5pv210_clk_ip1_ctrl,
		.ctrlbit	= (1<<16),
	}, {
		.name		= "usb-host",
		.id		= -1,
		.parent		= &clk_h133,
		.enable		= s5pv210_clk_ip1_ctrl,
		.ctrlbit	= (1<<17),
	}, {
		.name		= "lcd",
		.id		= -1,
		.parent		= &clk_h166,
		.enable		= s5pv210_clk_ip1_ctrl,
		.ctrlbit	= (1<<0),
	}, {
		.name		= "cfcon",
		.id		= 0,
		.parent		= &clk_h133,
		.enable		= s5pv210_clk_ip1_ctrl,
		.ctrlbit	= (1<<25),
	}, {
		.name		= "hsmmc",
		.id		= 0,
		.parent		= &clk_h133,
		.enable		= s5pv210_clk_ip2_ctrl,
		.ctrlbit	= (1<<16),
	}, {
		.name		= "hsmmc",
		.id		= 1,
		.parent		= &clk_h133,
		.enable		= s5pv210_clk_ip2_ctrl,
		.ctrlbit	= (1<<17),
	}, {
		.name		= "hsmmc",
		.id		= 2,
		.parent		= &clk_h133,
		.enable		= s5pv210_clk_ip2_ctrl,
		.ctrlbit	= (1<<18),
	}, {
		.name		= "hsmmc",
		.id		= 3,
		.parent		= &clk_h133,
		.enable		= s5pv210_clk_ip2_ctrl,
		.ctrlbit	= (1<<19),
	}, {
		.name		= "systimer",
		.id		= -1,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<16),
	}, {
		.name		= "watchdog",
		.id		= -1,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<22),
	}, {
		.name		= "rtc",
		.id		= -1,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<15),
	}, {
		.name		= "i2c",
		.id		= 0,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<7),
	}, {
		.name		= "i2c",
		.id		= 1,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<8),
	}, {
		.name		= "i2c",
		.id		= 2,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<9),
	}, {
		.name		= "spi",
		.id		= 0,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<12),
	}, {
		.name		= "spi",
		.id		= 1,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<13),
	}, {
		.name		= "spi",
		.id		= 2,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<14),
	}, {
		.name		= "timers",
		.id		= -1,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<23),
	}, {
		.name		= "adc",
		.id		= -1,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<24),
	}, {
		.name		= "keypad",
		.id		= -1,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<21),
	}, {
		.name		= "i2s_v50",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<4),
	}, {
		.name		= "i2s_v32",
		.id		= 0,
		.parent		= &clk_p,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<4),
	}, {
		.name		= "i2s_v32",
		.id		= 1,
		.parent		= &clk_p,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<4),
	}
};

static struct clk init_clocks[] = {
	{
		.name		= "uart",
		.id		= 0,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<7),
	}, {
		.name		= "uart",
		.id		= 1,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<8),
	}, {
		.name		= "uart",
		.id		= 2,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<9),
	}, {
		.name		= "uart",
		.id		= 3,
		.parent		= &clk_p66,
		.enable		= s5pv210_clk_ip3_ctrl,
		.ctrlbit	= (1<<10),
	},
};

static struct clksrc_clk clk_mout_apll = {
	.clk	= {
		.name		= "mout_apll",
		.id		= -1,
	},
	.sources	= &clk_src_apll,
	.reg_src	= { .reg = S5P_CLK_SRC0, .shift = 0, .size = 1 },
};

static struct clksrc_clk clk_mout_epll = {
	.clk	= {
		.name		= "mout_epll",
		.id		= -1,
	},
	.sources	= &clk_src_epll,
	.reg_src	= { .reg = S5P_CLK_SRC0, .shift = 8, .size = 1 },
};

static struct clksrc_clk clk_mout_mpll = {
	.clk = {
		.name		= "mout_mpll",
		.id		= -1,
	},
	.sources	= &clk_src_mpll,
	.reg_src	= { .reg = S5P_CLK_SRC0, .shift = 4, .size = 1 },
};

static struct clk *clkset_uart_list[] = {
	[6] = &clk_mout_mpll.clk,
	[7] = &clk_mout_epll.clk,
};

static struct clksrc_sources clkset_uart = {
	.sources	= clkset_uart_list,
	.nr_sources	= ARRAY_SIZE(clkset_uart_list),
};

static struct clksrc_clk clksrcs[] = {
	{
		.clk	= {
			.name		= "uclk1",
			.id		= -1,
			.ctrlbit	= (1<<17),
			.enable		= s5pv210_clk_ip3_ctrl,
		},
		.sources = &clkset_uart,
		.reg_src = { .reg = S5P_CLK_SRC4, .shift = 16, .size = 4 },
		.reg_div = { .reg = S5P_CLK_DIV4, .shift = 16, .size = 4 },
	}
};

/* Clock initialisation code */
static struct clksrc_clk *init_parents[] = {
	&clk_mout_apll,
	&clk_mout_epll,
	&clk_mout_mpll,
};

#define GET_DIV(clk, field) ((((clk) & field##_MASK) >> field##_SHIFT) + 1)

void __init_or_cpufreq s5pv210_setup_clocks(void)
{
	struct clk *xtal_clk;
	unsigned long xtal;
	unsigned long armclk;
	unsigned long hclk200;
	unsigned long hclk166;
	unsigned long hclk133;
	unsigned long pclk100;
	unsigned long pclk83;
	unsigned long pclk66;
	unsigned long apll;
	unsigned long mpll;
	unsigned long epll;
	unsigned int ptr;
	u32 clkdiv0, clkdiv1;

	printk(KERN_DEBUG "%s: registering clocks\n", __func__);

	clkdiv0 = __raw_readl(S5P_CLK_DIV0);
	clkdiv1 = __raw_readl(S5P_CLK_DIV1);

	printk(KERN_DEBUG "%s: clkdiv0 = %08x, clkdiv1 = %08x\n",
				__func__, clkdiv0, clkdiv1);

	xtal_clk = clk_get(NULL, "xtal");
	BUG_ON(IS_ERR(xtal_clk));

	xtal = clk_get_rate(xtal_clk);
	clk_put(xtal_clk);

	printk(KERN_DEBUG "%s: xtal is %ld\n", __func__, xtal);

	apll = s5p_get_pll45xx(xtal, __raw_readl(S5P_APLL_CON), pll_4508);
	mpll = s5p_get_pll45xx(xtal, __raw_readl(S5P_MPLL_CON), pll_4502);
	epll = s5p_get_pll45xx(xtal, __raw_readl(S5P_EPLL_CON), pll_4500);

	printk(KERN_INFO "S5PV210: PLL settings, A=%ld, M=%ld, E=%ld",
			apll, mpll, epll);

	armclk = apll / GET_DIV(clkdiv0, S5P_CLKDIV0_APLL);
	if (__raw_readl(S5P_CLK_SRC0) & S5P_CLKSRC0_MUX200_MASK)
		hclk200 = mpll / GET_DIV(clkdiv0, S5P_CLKDIV0_HCLK200);
	else
		hclk200 = armclk / GET_DIV(clkdiv0, S5P_CLKDIV0_HCLK200);

	if (__raw_readl(S5P_CLK_SRC0) & S5P_CLKSRC0_MUX166_MASK) {
		hclk166 = apll / GET_DIV(clkdiv0, S5P_CLKDIV0_A2M);
		hclk166 = hclk166 / GET_DIV(clkdiv0, S5P_CLKDIV0_HCLK166);
	} else
		hclk166 = mpll / GET_DIV(clkdiv0, S5P_CLKDIV0_HCLK166);

	if (__raw_readl(S5P_CLK_SRC0) & S5P_CLKSRC0_MUX133_MASK) {
		hclk133 = apll / GET_DIV(clkdiv0, S5P_CLKDIV0_A2M);
		hclk133 = hclk133 / GET_DIV(clkdiv0, S5P_CLKDIV0_HCLK133);
	} else
		hclk133 = mpll / GET_DIV(clkdiv0, S5P_CLKDIV0_HCLK133);

	pclk100 = hclk200 / GET_DIV(clkdiv0, S5P_CLKDIV0_PCLK100);
	pclk83 = hclk166 / GET_DIV(clkdiv0, S5P_CLKDIV0_PCLK83);
	pclk66 = hclk133 / GET_DIV(clkdiv0, S5P_CLKDIV0_PCLK66);

	printk(KERN_INFO "S5PV210: ARMCLK=%ld, HCLKM=%ld, HCLKD=%ld, \
			HCLKP=%ld, PCLKM=%ld, PCLKD=%ld, PCLKP=%ld\n",
	       armclk, hclk200, hclk166, hclk133, pclk100, pclk83, pclk66);

	clk_fout_apll.rate = apll;
	clk_fout_mpll.rate = mpll;
	clk_fout_epll.rate = epll;

	clk_f.rate = armclk;
	clk_h.rate = hclk133;
	clk_p.rate = pclk66;
	clk_p66.rate = pclk66;
	clk_p83.rate = pclk83;
	clk_h133.rate = hclk133;
	clk_h166.rate = hclk166;
	clk_h200.rate = hclk200;

	for (ptr = 0; ptr < ARRAY_SIZE(init_parents); ptr++)
		s3c_set_clksrc(init_parents[ptr], true);

	for (ptr = 0; ptr < ARRAY_SIZE(clksrcs); ptr++)
		s3c_set_clksrc(&clksrcs[ptr], true);
}

static struct clk *clks[] __initdata = {
	&clk_mout_epll.clk,
	&clk_mout_mpll.clk,
};

void __init s5pv210_register_clocks(void)
{
	struct clk *clkp;
	int ret;
	int ptr;

	ret = s3c24xx_register_clocks(clks, ARRAY_SIZE(clks));
	if (ret > 0)
		printk(KERN_ERR "Failed to register %u clocks\n", ret);

	s3c_register_clksrc(clksrcs, ARRAY_SIZE(clksrcs));
	s3c_register_clocks(init_clocks, ARRAY_SIZE(init_clocks));

	ret = s3c24xx_register_clocks(sys_clks, ARRAY_SIZE(sys_clks));
	if (ret > 0)
		printk(KERN_ERR "Failed to register system clocks\n");

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
