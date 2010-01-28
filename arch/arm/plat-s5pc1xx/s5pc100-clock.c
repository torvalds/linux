/* linux/arch/arm/plat-s5pc1xx/s5pc100-clock.c
 *
 * Copyright 2009 Samsung Electronics, Co.
 *	Byungho Min <bhmin@samsung.com>
 *
 * S5PC100 based common clock support
 *
 * Based on plat-s3c64xx/s3c6400-clock.c
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

#include <mach/hardware.h>
#include <mach/map.h>

#include <plat/cpu-freq.h>

#include <plat/regs-clock.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/pll.h>
#include <plat/devs.h>
#include <plat/s5pc100.h>

/* fin_apll, fin_mpll and fin_epll are all the same clock, which we call
 * ext_xtal_mux for want of an actual name from the manual.
*/

static struct clk clk_ext_xtal_mux = {
	.name		= "ext_xtal",
	.id		= -1,
};

#define clk_fin_apll clk_ext_xtal_mux
#define clk_fin_mpll clk_ext_xtal_mux
#define clk_fin_epll clk_ext_xtal_mux
#define clk_fin_hpll clk_ext_xtal_mux

#define clk_fout_mpll	clk_mpll
#define clk_vclk_54m	clk_54m

struct clk_sources {
	unsigned int	nr_sources;
	struct clk	**sources;
};

struct clksrc_clk {
	struct clk		clk;
	unsigned int		mask;
	unsigned int		shift;

	struct clk_sources	*sources;

	unsigned int		divider_shift;
	void __iomem		*reg_divider;
	void __iomem		*reg_source;
};

/* APLL */
static struct clk clk_fout_apll = {
	.name		= "fout_apll",
	.id		= -1,
	.rate		= 27000000,
};

static struct clk *clk_src_apll_list[] = {
	[0] = &clk_fin_apll,
	[1] = &clk_fout_apll,
};

static struct clk_sources clk_src_apll = {
	.sources	= clk_src_apll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_apll_list),
};

static struct clksrc_clk clk_mout_apll = {
	.clk	= {
		.name		= "mout_apll",
		.id		= -1,
	},
	.shift		= S5PC100_CLKSRC0_APLL_SHIFT,
	.mask		= S5PC100_CLKSRC0_APLL_MASK,
	.sources	= &clk_src_apll,
	.reg_source	= S5PC100_CLKSRC0,
};

static unsigned long s5pc100_clk_dout_apll_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned int ratio;

	ratio = __raw_readl(S5PC100_CLKDIV0) & S5PC100_CLKDIV0_APLL_MASK;
	ratio >>= S5PC100_CLKDIV0_APLL_SHIFT;

	return rate / (ratio + 1);
}

static struct clk clk_dout_apll = {
	.name		= "dout_apll",
	.id		= -1,
	.parent		= &clk_mout_apll.clk,
	.get_rate	= s5pc100_clk_dout_apll_get_rate,
};

static unsigned long s5pc100_clk_arm_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned int ratio;

	ratio = __raw_readl(S5PC100_CLKDIV0) & S5PC100_CLKDIV0_ARM_MASK;
	ratio >>= S5PC100_CLKDIV0_ARM_SHIFT;

	return rate / (ratio + 1);
}

static unsigned long s5pc100_clk_arm_round_rate(struct clk *clk,
						unsigned long rate)
{
	unsigned long parent = clk_get_rate(clk->parent);
	u32 div;

	if (parent < rate)
		return rate;

	div = (parent / rate) - 1;
	if (div > S5PC100_CLKDIV0_ARM_MASK)
		div = S5PC100_CLKDIV0_ARM_MASK;

	return parent / (div + 1);
}

static int s5pc100_clk_arm_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long parent = clk_get_rate(clk->parent);
	u32 div;
	u32 val;

	if (rate < parent / (S5PC100_CLKDIV0_ARM_MASK + 1))
		return -EINVAL;

	rate = clk_round_rate(clk, rate);
	div = clk_get_rate(clk->parent) / rate;

	val = __raw_readl(S5PC100_CLKDIV0);
	val &= S5PC100_CLKDIV0_ARM_MASK;
	val |= (div - 1);
	__raw_writel(val, S5PC100_CLKDIV0);

	return 0;
}

static struct clk clk_arm = {
	.name		= "armclk",
	.id		= -1,
	.parent		= &clk_dout_apll,
	.get_rate	= s5pc100_clk_arm_get_rate,
	.set_rate	= s5pc100_clk_arm_set_rate,
	.round_rate	= s5pc100_clk_arm_round_rate,
};

static unsigned long s5pc100_clk_dout_d0_bus_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned int ratio;

	ratio = __raw_readl(S5PC100_CLKDIV0) & S5PC100_CLKDIV0_D0_MASK;
	ratio >>= S5PC100_CLKDIV0_D0_SHIFT;

	return rate / (ratio + 1);
}

static struct clk clk_dout_d0_bus = {
	.name		= "dout_d0_bus",
	.id		= -1,
	.parent		= &clk_arm,
	.get_rate	= s5pc100_clk_dout_d0_bus_get_rate,
};

static unsigned long s5pc100_clk_dout_pclkd0_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned int ratio;

	ratio = __raw_readl(S5PC100_CLKDIV0) & S5PC100_CLKDIV0_PCLKD0_MASK;
	ratio >>= S5PC100_CLKDIV0_PCLKD0_SHIFT;

	return rate / (ratio + 1);
}

static struct clk clk_dout_pclkd0 = {
	.name		= "dout_pclkd0",
	.id		= -1,
	.parent		= &clk_dout_d0_bus,
	.get_rate	= s5pc100_clk_dout_pclkd0_get_rate,
};

static unsigned long s5pc100_clk_dout_apll2_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned int ratio;

	ratio = __raw_readl(S5PC100_CLKDIV1) & S5PC100_CLKDIV1_APLL2_MASK;
	ratio >>= S5PC100_CLKDIV1_APLL2_SHIFT;

	return rate / (ratio + 1);
}

static struct clk clk_dout_apll2 = {
	.name		= "dout_apll2",
	.id		= -1,
	.parent		= &clk_mout_apll.clk,
	.get_rate	= s5pc100_clk_dout_apll2_get_rate,
};

/* MPLL */
static struct clk *clk_src_mpll_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &clk_fout_mpll,
};

static struct clk_sources clk_src_mpll = {
	.sources	= clk_src_mpll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mpll_list),
};

static struct clksrc_clk clk_mout_mpll = {
	.clk = {
		.name		= "mout_mpll",
		.id		= -1,
	},
	.shift		= S5PC100_CLKSRC0_MPLL_SHIFT,
	.mask		= S5PC100_CLKSRC0_MPLL_MASK,
	.sources	= &clk_src_mpll,
	.reg_source	= S5PC100_CLKSRC0,
};

static struct clk *clkset_am_list[] = {
	[0] = &clk_mout_mpll.clk,
	[1] = &clk_dout_apll2,
};

static struct clk_sources clk_src_am = {
	.sources	= clkset_am_list,
	.nr_sources	= ARRAY_SIZE(clkset_am_list),
};

static struct clksrc_clk clk_mout_am = {
	.clk = {
		.name		= "mout_am",
		.id		= -1,
	},
	.shift		= S5PC100_CLKSRC0_AMMUX_SHIFT,
	.mask		= S5PC100_CLKSRC0_AMMUX_MASK,
	.sources	= &clk_src_am,
	.reg_source	= S5PC100_CLKSRC0,
};

static unsigned long s5pc100_clk_dout_d1_bus_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned int ratio;

	printk(KERN_DEBUG "%s: parent is %ld\n", __func__, rate);

	ratio = __raw_readl(S5PC100_CLKDIV1) & S5PC100_CLKDIV1_D1_MASK;
	ratio >>= S5PC100_CLKDIV1_D1_SHIFT;

	return rate / (ratio + 1);
}

static struct clk clk_dout_d1_bus = {
	.name		= "dout_d1_bus",
	.id		= -1,
	.parent		= &clk_mout_am.clk,
	.get_rate	= s5pc100_clk_dout_d1_bus_get_rate,
};

static struct clk *clkset_onenand_list[] = {
	[0] = &clk_dout_d0_bus,
	[1] = &clk_dout_d1_bus,
};

static struct clk_sources clk_src_onenand = {
	.sources	= clkset_onenand_list,
	.nr_sources	= ARRAY_SIZE(clkset_onenand_list),
};

static struct clksrc_clk clk_mout_onenand = {
	.clk = {
		.name		= "mout_onenand",
		.id		= -1,
	},
	.shift		= S5PC100_CLKSRC0_ONENAND_SHIFT,
	.mask		= S5PC100_CLKSRC0_ONENAND_MASK,
	.sources	= &clk_src_onenand,
	.reg_source	= S5PC100_CLKSRC0,
};

static unsigned long s5pc100_clk_dout_pclkd1_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned int ratio;

	printk(KERN_DEBUG "%s: parent is %ld\n", __func__, rate);

	ratio = __raw_readl(S5PC100_CLKDIV1) & S5PC100_CLKDIV1_PCLKD1_MASK;
	ratio >>= S5PC100_CLKDIV1_PCLKD1_SHIFT;

	return rate / (ratio + 1);
}

static struct clk clk_dout_pclkd1 = {
	.name		= "dout_pclkd1",
	.id		= -1,
	.parent		= &clk_dout_d1_bus,
	.get_rate	= s5pc100_clk_dout_pclkd1_get_rate,
};

static unsigned long s5pc100_clk_dout_mpll2_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned int ratio;

	printk(KERN_DEBUG "%s: parent is %ld\n", __func__, rate);

	ratio = __raw_readl(S5PC100_CLKDIV1) & S5PC100_CLKDIV1_MPLL2_MASK;
	ratio >>= S5PC100_CLKDIV1_MPLL2_SHIFT;

	return rate / (ratio + 1);
}

static struct clk clk_dout_mpll2 = {
	.name		= "dout_mpll2",
	.id		= -1,
	.parent		= &clk_mout_am.clk,
	.get_rate	= s5pc100_clk_dout_mpll2_get_rate,
};

static unsigned long s5pc100_clk_dout_cam_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned int ratio;

	printk(KERN_DEBUG "%s: parent is %ld\n", __func__, rate);

	ratio = __raw_readl(S5PC100_CLKDIV1) & S5PC100_CLKDIV1_CAM_MASK;
	ratio >>= S5PC100_CLKDIV1_CAM_SHIFT;

	return rate / (ratio + 1);
}

static struct clk clk_dout_cam = {
	.name		= "dout_cam",
	.id		= -1,
	.parent		= &clk_dout_mpll2,
	.get_rate	= s5pc100_clk_dout_cam_get_rate,
};

static unsigned long s5pc100_clk_dout_mpll_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	unsigned int ratio;

	printk(KERN_DEBUG "%s: parent is %ld\n", __func__, rate);

	ratio = __raw_readl(S5PC100_CLKDIV1) & S5PC100_CLKDIV1_MPLL_MASK;
	ratio >>= S5PC100_CLKDIV1_MPLL_SHIFT;

	return rate / (ratio + 1);
}

static struct clk clk_dout_mpll = {
	.name		= "dout_mpll",
	.id		= -1,
	.parent		= &clk_mout_am.clk,
	.get_rate	= s5pc100_clk_dout_mpll_get_rate,
};

/* EPLL */
static struct clk clk_fout_epll = {
	.name		= "fout_epll",
	.id		= -1,
};

static struct clk *clk_src_epll_list[] = {
	[0] = &clk_fin_epll,
	[1] = &clk_fout_epll,
};

static struct clk_sources clk_src_epll = {
	.sources	= clk_src_epll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_epll_list),
};

static struct clksrc_clk clk_mout_epll = {
	.clk	= {
		.name		= "mout_epll",
		.id		= -1,
	},
	.shift		= S5PC100_CLKSRC0_EPLL_SHIFT,
	.mask		= S5PC100_CLKSRC0_EPLL_MASK,
	.sources	= &clk_src_epll,
	.reg_source	= S5PC100_CLKSRC0,
};

/* HPLL */
static struct clk clk_fout_hpll = {
	.name		= "fout_hpll",
	.id		= -1,
};

static struct clk *clk_src_hpll_list[] = {
	[0] = &clk_27m,
	[1] = &clk_fout_hpll,
};

static struct clk_sources clk_src_hpll = {
	.sources	= clk_src_hpll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_hpll_list),
};

static struct clksrc_clk clk_mout_hpll = {
	.clk	= {
		.name		= "mout_hpll",
		.id		= -1,
	},
	.shift		= S5PC100_CLKSRC0_HPLL_SHIFT,
	.mask		= S5PC100_CLKSRC0_HPLL_MASK,
	.sources	= &clk_src_hpll,
	.reg_source	= S5PC100_CLKSRC0,
};

/* Peripherals */
/*
 * The peripheral clocks are all controlled via clocksource followed
 * by an optional divider and gate stage. We currently roll this into
 * one clock which hides the intermediate clock from the mux.
 *
 * Note, the JPEG clock can only be an even divider...
 *
 * The scaler and LCD clocks depend on the S5PC100 version, and also
 * have a common parent divisor so are not included here.
 */

static inline struct clksrc_clk *to_clksrc(struct clk *clk)
{
	return container_of(clk, struct clksrc_clk, clk);
}

static unsigned long s5pc100_getrate_clksrc(struct clk *clk)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	unsigned long rate = clk_get_rate(clk->parent);
	u32 clkdiv = __raw_readl(sclk->reg_divider);

	clkdiv >>= sclk->divider_shift;
	clkdiv &= 0xf;
	clkdiv++;

	rate /= clkdiv;
	return rate;
}

static int s5pc100_setrate_clksrc(struct clk *clk, unsigned long rate)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	void __iomem *reg = sclk->reg_divider;
	unsigned int div;
	u32 val;

	rate = clk_round_rate(clk, rate);
	div = clk_get_rate(clk->parent) / rate;
	if (div > 16)
		return -EINVAL;

	val = __raw_readl(reg);
	val &= ~(0xf << sclk->divider_shift);
	val |= (div - 1) << sclk->divider_shift;
	__raw_writel(val, reg);

	return 0;
}

static int s5pc100_setparent_clksrc(struct clk *clk, struct clk *parent)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	struct clk_sources *srcs = sclk->sources;
	u32 clksrc = __raw_readl(sclk->reg_source);
	int src_nr = -1;
	int ptr;

	for (ptr = 0; ptr < srcs->nr_sources; ptr++)
		if (srcs->sources[ptr] == parent) {
			src_nr = ptr;
			break;
		}

	if (src_nr >= 0) {
		clksrc &= ~sclk->mask;
		clksrc |= src_nr << sclk->shift;

		__raw_writel(clksrc, sclk->reg_source);
		return 0;
	}

	return -EINVAL;
}

static unsigned long s5pc100_roundrate_clksrc(struct clk *clk,
					      unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(clk->parent);
	int div;

	if (rate > parent_rate)
		rate = parent_rate;
	else {
		div = rate / parent_rate;

		if (div == 0)
			div = 1;
		if (div > 16)
			div = 16;

		rate = parent_rate / div;
	}

	return rate;
}

static struct clk *clkset_spi_list[] = {
	&clk_mout_epll.clk,
	&clk_dout_mpll2,
	&clk_fin_epll,
	&clk_mout_hpll.clk,
};

static struct clk_sources clkset_spi = {
	.sources	= clkset_spi_list,
	.nr_sources	= ARRAY_SIZE(clkset_spi_list),
};

static struct clksrc_clk clk_spi0 = {
	.clk	= {
		.name		= "spi_bus",
		.id		= 0,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_SPI0,
		.enable		= s5pc100_sclk0_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC1_SPI0_SHIFT,
	.mask		= S5PC100_CLKSRC1_SPI0_MASK,
	.sources	= &clkset_spi,
	.divider_shift	= S5PC100_CLKDIV2_SPI0_SHIFT,
	.reg_divider	= S5PC100_CLKDIV2,
	.reg_source	= S5PC100_CLKSRC1,
};

static struct clksrc_clk clk_spi1 = {
	.clk	= {
		.name		= "spi_bus",
		.id		= 1,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_SPI1,
		.enable		= s5pc100_sclk0_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC1_SPI1_SHIFT,
	.mask		= S5PC100_CLKSRC1_SPI1_MASK,
	.sources	= &clkset_spi,
	.divider_shift	= S5PC100_CLKDIV2_SPI1_SHIFT,
	.reg_divider	= S5PC100_CLKDIV2,
	.reg_source	= S5PC100_CLKSRC1,
};

static struct clksrc_clk clk_spi2 = {
	.clk	= {
		.name		= "spi_bus",
		.id		= 2,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_SPI2,
		.enable		= s5pc100_sclk0_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC1_SPI2_SHIFT,
	.mask		= S5PC100_CLKSRC1_SPI2_MASK,
	.sources	= &clkset_spi,
	.divider_shift	= S5PC100_CLKDIV2_SPI2_SHIFT,
	.reg_divider	= S5PC100_CLKDIV2,
	.reg_source	= S5PC100_CLKSRC1,
};

static struct clk *clkset_uart_list[] = {
	&clk_mout_epll.clk,
	&clk_dout_mpll,
};

static struct clk_sources clkset_uart = {
	.sources	= clkset_uart_list,
	.nr_sources	= ARRAY_SIZE(clkset_uart_list),
};

static struct clksrc_clk clk_uart_uclk1 = {
	.clk	= {
		.name		= "uclk1",
		.id		= -1,
		.ctrlbit        = S5PC100_CLKGATE_SCLK0_UART,
		.enable		= s5pc100_sclk0_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC1_UART_SHIFT,
	.mask		= S5PC100_CLKSRC1_UART_MASK,
	.sources	= &clkset_uart,
	.divider_shift	= S5PC100_CLKDIV2_UART_SHIFT,
	.reg_divider	= S5PC100_CLKDIV2,
	.reg_source	= S5PC100_CLKSRC1,
};

static struct clk clk_iis_cd0 = {
	.name		= "iis_cdclk0",
	.id		= -1,
};

static struct clk clk_iis_cd1 = {
	.name		= "iis_cdclk1",
	.id		= -1,
};

static struct clk clk_iis_cd2 = {
	.name		= "iis_cdclk2",
	.id		= -1,
};

static struct clk clk_pcm_cd0 = {
	.name		= "pcm_cdclk0",
	.id		= -1,
};

static struct clk clk_pcm_cd1 = {
	.name		= "pcm_cdclk1",
	.id		= -1,
};

static struct clk *clkset_audio0_list[] = {
	&clk_mout_epll.clk,
	&clk_dout_mpll,
	&clk_fin_epll,
	&clk_iis_cd0,
	&clk_pcm_cd0,
	&clk_mout_hpll.clk,
};

static struct clk_sources clkset_audio0 = {
	.sources	= clkset_audio0_list,
	.nr_sources	= ARRAY_SIZE(clkset_audio0_list),
};

static struct clksrc_clk clk_audio0 = {
	.clk	= {
		.name		= "audio-bus",
		.id		= 0,
		.ctrlbit	= S5PC100_CLKGATE_SCLK1_AUDIO0,
		.enable		= s5pc100_sclk1_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC3_AUDIO0_SHIFT,
	.mask		= S5PC100_CLKSRC3_AUDIO0_MASK,
	.sources	= &clkset_audio0,
	.divider_shift	= S5PC100_CLKDIV4_AUDIO0_SHIFT,
	.reg_divider	= S5PC100_CLKDIV4,
	.reg_source	= S5PC100_CLKSRC3,
};

static struct clk *clkset_audio1_list[] = {
	&clk_mout_epll.clk,
	&clk_dout_mpll,
	&clk_fin_epll,
	&clk_iis_cd1,
	&clk_pcm_cd1,
	&clk_mout_hpll.clk,
};

static struct clk_sources clkset_audio1 = {
	.sources	= clkset_audio1_list,
	.nr_sources	= ARRAY_SIZE(clkset_audio1_list),
};

static struct clksrc_clk clk_audio1 = {
	.clk	= {
		.name		= "audio-bus",
		.id		= 1,
		.ctrlbit	= S5PC100_CLKGATE_SCLK1_AUDIO1,
		.enable		= s5pc100_sclk1_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC3_AUDIO1_SHIFT,
	.mask		= S5PC100_CLKSRC3_AUDIO1_MASK,
	.sources	= &clkset_audio1,
	.divider_shift	= S5PC100_CLKDIV4_AUDIO1_SHIFT,
	.reg_divider	= S5PC100_CLKDIV4,
	.reg_source	= S5PC100_CLKSRC3,
};

static struct clk *clkset_audio2_list[] = {
	&clk_mout_epll.clk,
	&clk_dout_mpll,
	&clk_fin_epll,
	&clk_iis_cd2,
	&clk_mout_hpll.clk,
};

static struct clk_sources clkset_audio2 = {
	.sources	= clkset_audio2_list,
	.nr_sources	= ARRAY_SIZE(clkset_audio2_list),
};

static struct clksrc_clk clk_audio2 = {
	.clk	= {
		.name		= "audio-bus",
		.id		= 2,
		.ctrlbit	= S5PC100_CLKGATE_SCLK1_AUDIO2,
		.enable		= s5pc100_sclk1_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC3_AUDIO2_SHIFT,
	.mask		= S5PC100_CLKSRC3_AUDIO2_MASK,
	.sources	= &clkset_audio2,
	.divider_shift	= S5PC100_CLKDIV4_AUDIO2_SHIFT,
	.reg_divider	= S5PC100_CLKDIV4,
	.reg_source	= S5PC100_CLKSRC3,
};

static struct clk *clkset_spdif_list[] = {
	&clk_audio0.clk,
	&clk_audio1.clk,
	&clk_audio2.clk,
};

static struct clk_sources clkset_spdif = {
	.sources	= clkset_spdif_list,
	.nr_sources	= ARRAY_SIZE(clkset_spdif_list),
};

static struct clksrc_clk clk_spdif = {
	.clk	= {
		.name		= "spdif",
		.id		= -1,
	},
	.shift		= S5PC100_CLKSRC3_SPDIF_SHIFT,
	.mask		= S5PC100_CLKSRC3_SPDIF_MASK,
	.sources	= &clkset_spdif,
	.reg_source	= S5PC100_CLKSRC3,
};

static struct clk *clkset_lcd_fimc_list[] = {
	&clk_mout_epll.clk,
	&clk_dout_mpll,
	&clk_mout_hpll.clk,
	&clk_vclk_54m,
};

static struct clk_sources clkset_lcd_fimc = {
	.sources	= clkset_lcd_fimc_list,
	.nr_sources	= ARRAY_SIZE(clkset_lcd_fimc_list),
};

static struct clksrc_clk clk_lcd = {
	.clk	= {
		.name		= "lcd",
		.id		= -1,
		.ctrlbit	= S5PC100_CLKGATE_SCLK1_LCD,
		.enable		= s5pc100_sclk1_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC2_LCD_SHIFT,
	.mask		= S5PC100_CLKSRC2_LCD_MASK,
	.sources	= &clkset_lcd_fimc,
	.divider_shift	= S5PC100_CLKDIV3_LCD_SHIFT,
	.reg_divider	= S5PC100_CLKDIV3,
	.reg_source	= S5PC100_CLKSRC2,
};

static struct clksrc_clk clk_fimc0 = {
	.clk	= {
		.name		= "fimc",
		.id		= 0,
		.ctrlbit	= S5PC100_CLKGATE_SCLK1_FIMC0,
		.enable		= s5pc100_sclk1_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC2_FIMC0_SHIFT,
	.mask		= S5PC100_CLKSRC2_FIMC0_MASK,
	.sources	= &clkset_lcd_fimc,
	.divider_shift	= S5PC100_CLKDIV3_FIMC0_SHIFT,
	.reg_divider	= S5PC100_CLKDIV3,
	.reg_source	= S5PC100_CLKSRC2,
};

static struct clksrc_clk clk_fimc1 = {
	.clk	= {
		.name		= "fimc",
		.id		= 1,
		.ctrlbit	= S5PC100_CLKGATE_SCLK1_FIMC1,
		.enable		= s5pc100_sclk1_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC2_FIMC1_SHIFT,
	.mask		= S5PC100_CLKSRC2_FIMC1_MASK,
	.sources	= &clkset_lcd_fimc,
	.divider_shift	= S5PC100_CLKDIV3_FIMC1_SHIFT,
	.reg_divider	= S5PC100_CLKDIV3,
	.reg_source	= S5PC100_CLKSRC2,
};

static struct clksrc_clk clk_fimc2 = {
	.clk	= {
		.name		= "fimc",
		.id		= 2,
		.ctrlbit	= S5PC100_CLKGATE_SCLK1_FIMC2,
		.enable		= s5pc100_sclk1_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC2_FIMC2_SHIFT,
	.mask		= S5PC100_CLKSRC2_FIMC2_MASK,
	.sources	= &clkset_lcd_fimc,
	.divider_shift	= S5PC100_CLKDIV3_FIMC2_SHIFT,
	.reg_divider	= S5PC100_CLKDIV3,
	.reg_source	= S5PC100_CLKSRC2,
};

static struct clk *clkset_mmc_list[] = {
	&clk_mout_epll.clk,
	&clk_dout_mpll,
	&clk_fin_epll,
	&clk_mout_hpll.clk ,
};

static struct clk_sources clkset_mmc = {
	.sources	= clkset_mmc_list,
	.nr_sources	= ARRAY_SIZE(clkset_mmc_list),
};

static struct clksrc_clk clk_mmc0 = {
	.clk	= {
		.name		= "mmc_bus",
		.id		= 0,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_MMC0,
		.enable		= s5pc100_sclk0_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC2_MMC0_SHIFT,
	.mask		= S5PC100_CLKSRC2_MMC0_MASK,
	.sources	= &clkset_mmc,
	.divider_shift	= S5PC100_CLKDIV3_MMC0_SHIFT,
	.reg_divider	= S5PC100_CLKDIV3,
	.reg_source	= S5PC100_CLKSRC2,
};

static struct clksrc_clk clk_mmc1 = {
	.clk	= {
		.name		= "mmc_bus",
		.id		= 1,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_MMC1,
		.enable		= s5pc100_sclk0_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC2_MMC1_SHIFT,
	.mask		= S5PC100_CLKSRC2_MMC1_MASK,
	.sources	= &clkset_mmc,
	.divider_shift	= S5PC100_CLKDIV3_MMC1_SHIFT,
	.reg_divider	= S5PC100_CLKDIV3,
	.reg_source	= S5PC100_CLKSRC2,
};

static struct clksrc_clk clk_mmc2 = {
	.clk	= {
		.name		= "mmc_bus",
		.id		= 2,
		.ctrlbit	= S5PC100_CLKGATE_SCLK0_MMC2,
		.enable		= s5pc100_sclk0_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC2_MMC2_SHIFT,
	.mask		= S5PC100_CLKSRC2_MMC2_MASK,
	.sources	= &clkset_mmc,
	.divider_shift	= S5PC100_CLKDIV3_MMC2_SHIFT,
	.reg_divider	= S5PC100_CLKDIV3,
	.reg_source	= S5PC100_CLKSRC2,
};


static struct clk *clkset_usbhost_list[] = {
	&clk_mout_epll.clk,
	&clk_dout_mpll,
	&clk_mout_hpll.clk,
	&clk_48m,
};

static struct clk_sources clkset_usbhost = {
	.sources	= clkset_usbhost_list,
	.nr_sources	= ARRAY_SIZE(clkset_usbhost_list),
};

static struct clksrc_clk clk_usbhost = {
	.clk	= {
		.name		= "usbhost",
		.id		= -1,
		.ctrlbit        = S5PC100_CLKGATE_SCLK0_USBHOST,
		.enable		= s5pc100_sclk0_ctrl,
		.set_parent	= s5pc100_setparent_clksrc,
		.get_rate	= s5pc100_getrate_clksrc,
		.set_rate	= s5pc100_setrate_clksrc,
		.round_rate	= s5pc100_roundrate_clksrc,
	},
	.shift		= S5PC100_CLKSRC1_UHOST_SHIFT,
	.mask		= S5PC100_CLKSRC1_UHOST_MASK,
	.sources	= &clkset_usbhost,
	.divider_shift	= S5PC100_CLKDIV2_UHOST_SHIFT,
	.reg_divider	= S5PC100_CLKDIV2,
	.reg_source	= S5PC100_CLKSRC1,
};

/* Clock initialisation code */

static struct clksrc_clk *init_parents[] = {
	&clk_mout_apll,
	&clk_mout_mpll,
	&clk_mout_am,
	&clk_mout_onenand,
	&clk_mout_epll,
	&clk_mout_hpll,
	&clk_spi0,
	&clk_spi1,
	&clk_spi2,
	&clk_uart_uclk1,
	&clk_audio0,
	&clk_audio1,
	&clk_audio2,
	&clk_spdif,
	&clk_lcd,
	&clk_fimc0,
	&clk_fimc1,
	&clk_fimc2,
	&clk_mmc0,
	&clk_mmc1,
	&clk_mmc2,
	&clk_usbhost,
};

static void __init_or_cpufreq s5pc100_set_clksrc(struct clksrc_clk *clk)
{
	struct clk_sources *srcs = clk->sources;
	u32 clksrc = __raw_readl(clk->reg_source);

	clksrc &= clk->mask;
	clksrc >>= clk->shift;

	if (clksrc > srcs->nr_sources || !srcs->sources[clksrc]) {
		printk(KERN_ERR "%s: bad source %d\n",
		       clk->clk.name, clksrc);
		return;
	}

	clk->clk.parent = srcs->sources[clksrc];

	printk(KERN_INFO "%s: source is %s (%d), rate is %ld.%03ld MHz\n",
		clk->clk.name, clk->clk.parent->name, clksrc,
		print_mhz(clk_get_rate(&clk->clk)));
}

#define GET_DIV(clk, field) ((((clk) & field##_MASK) >> field##_SHIFT) + 1)

void __init_or_cpufreq s5pc100_setup_clocks(void)
{
	struct clk *xtal_clk;
	unsigned long xtal;
	unsigned long armclk;
	unsigned long hclkd0;
	unsigned long hclk;
	unsigned long pclkd0;
	unsigned long pclk;
	unsigned long apll, mpll, epll, hpll;
	unsigned int ptr;
	u32 clkdiv0, clkdiv1;

	printk(KERN_DEBUG "%s: registering clocks\n", __func__);

	clkdiv0 = __raw_readl(S5PC100_CLKDIV0);
	clkdiv1 = __raw_readl(S5PC100_CLKDIV1);

	printk(KERN_DEBUG "%s: clkdiv0 = %08x, clkdiv1 = %08x\n", __func__, clkdiv0, clkdiv1);

	xtal_clk = clk_get(NULL, "xtal");
	BUG_ON(IS_ERR(xtal_clk));

	xtal = clk_get_rate(xtal_clk);
	clk_put(xtal_clk);

	printk(KERN_DEBUG "%s: xtal is %ld\n", __func__, xtal);

	apll = s5pc1xx_get_pll(xtal, __raw_readl(S5PC100_APLL_CON));
	mpll = s5pc1xx_get_pll(xtal, __raw_readl(S5PC100_MPLL_CON));
	epll = s5pc1xx_get_pll(xtal, __raw_readl(S5PC100_EPLL_CON));
	hpll = s5pc1xx_get_pll(xtal, __raw_readl(S5PC100_HPLL_CON));

	printk(KERN_INFO "S5PC100: Apll=%ld.%03ld Mhz, Mpll=%ld.%03ld Mhz"
		", Epll=%ld.%03ld Mhz, Hpll=%ld.%03ld Mhz\n",
		print_mhz(apll), print_mhz(mpll),
		print_mhz(epll), print_mhz(hpll));

	armclk = apll / GET_DIV(clkdiv0, S5PC100_CLKDIV0_APLL);
	armclk = armclk / GET_DIV(clkdiv0, S5PC100_CLKDIV0_ARM);
	hclkd0 = armclk / GET_DIV(clkdiv0, S5PC100_CLKDIV0_D0);
	pclkd0 = hclkd0 / GET_DIV(clkdiv0, S5PC100_CLKDIV0_PCLKD0);
	hclk = mpll / GET_DIV(clkdiv1, S5PC100_CLKDIV1_D1);
	pclk = hclk / GET_DIV(clkdiv1, S5PC100_CLKDIV1_PCLKD1);

	printk(KERN_INFO "S5PC100: ARMCLK=%ld.%03ld MHz, HCLKD0=%ld.%03ld MHz,"
		" PCLKD0=%ld.%03ld MHz\n, HCLK=%ld.%03ld MHz,"
		" PCLK=%ld.%03ld MHz\n",
		print_mhz(armclk), print_mhz(hclkd0),
		print_mhz(pclkd0), print_mhz(hclk), print_mhz(pclk));

	clk_fout_apll.rate = apll;
	clk_fout_mpll.rate = mpll;
	clk_fout_epll.rate = epll;
	clk_fout_hpll.rate = hpll;

	clk_h.rate = hclk;
	clk_p.rate = pclk;
	clk_f.rate = armclk;

	for (ptr = 0; ptr < ARRAY_SIZE(init_parents); ptr++)
		s5pc100_set_clksrc(init_parents[ptr]);
}

static struct clk *clks[] __initdata = {
	&clk_ext_xtal_mux,
	&clk_mout_apll.clk,
	&clk_dout_apll,
	&clk_dout_d0_bus,
	&clk_dout_pclkd0,
	&clk_dout_apll2,
	&clk_mout_mpll.clk,
	&clk_mout_am.clk,
	&clk_dout_d1_bus,
	&clk_mout_onenand.clk,
	&clk_dout_pclkd1,
	&clk_dout_mpll2,
	&clk_dout_cam,
	&clk_dout_mpll,
	&clk_mout_epll.clk,
	&clk_fout_epll,
	&clk_iis_cd0,
	&clk_iis_cd1,
	&clk_iis_cd2,
	&clk_pcm_cd0,
	&clk_pcm_cd1,
	&clk_spi0.clk,
	&clk_spi1.clk,
	&clk_spi2.clk,
	&clk_uart_uclk1.clk,
	&clk_audio0.clk,
	&clk_audio1.clk,
	&clk_audio2.clk,
	&clk_spdif.clk,
	&clk_lcd.clk,
	&clk_fimc0.clk,
	&clk_fimc1.clk,
	&clk_fimc2.clk,
	&clk_mmc0.clk,
	&clk_mmc1.clk,
	&clk_mmc2.clk,
	&clk_usbhost.clk,
	&clk_arm,
};

void __init s5pc100_register_clocks(void)
{
	struct clk *clkp;
	int ret;
	int ptr;

	for (ptr = 0; ptr < ARRAY_SIZE(clks); ptr++) {
		clkp = clks[ptr];
		ret = s3c24xx_register_clock(clkp);
		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
	}
}
