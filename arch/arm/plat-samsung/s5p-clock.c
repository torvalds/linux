/*
 * Copyright 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P - Common clock support
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
#include <linux/device.h>
#include <linux/io.h>
#include <asm/div64.h>

#include <mach/regs-clock.h>

#include <plat/clock.h>
#include <plat/clock-clksrc.h>
#include <plat/s5p-clock.h>

/* fin_apll, fin_mpll and fin_epll are all the same clock, which we call
 * clk_ext_xtal_mux.
*/
struct clk clk_ext_xtal_mux = {
	.name		= "ext_xtal",
	.id		= -1,
};

struct clk clk_xusbxti = {
	.name		= "xusbxti",
	.id		= -1,
};

struct clk s5p_clk_27m = {
	.name		= "clk_27m",
	.id		= -1,
	.rate		= 27000000,
};

/* 48MHz USB Phy clock output */
struct clk clk_48m = {
	.name		= "clk_48m",
	.id		= -1,
	.rate		= 48000000,
};

/* APLL clock output
 * No need .ctrlbit, this is always on
*/
struct clk clk_fout_apll = {
	.name		= "fout_apll",
	.id		= -1,
};

/* BPLL clock output */

struct clk clk_fout_bpll = {
	.name		= "fout_bpll",
	.id		= -1,
};

/* CPLL clock output */

struct clk clk_fout_cpll = {
	.name		= "fout_cpll",
	.id		= -1,
};

/* MPLL clock output
 * No need .ctrlbit, this is always on
*/
struct clk clk_fout_mpll = {
	.name		= "fout_mpll",
	.id		= -1,
};

/* EPLL clock output */
struct clk clk_fout_epll = {
	.name		= "fout_epll",
	.id		= -1,
	.ctrlbit	= (1 << 31),
};

/* DPLL clock output */
struct clk clk_fout_dpll = {
	.name		= "fout_dpll",
	.id		= -1,
	.ctrlbit	= (1 << 31),
};

/* VPLL clock output */
struct clk clk_fout_vpll = {
	.name		= "fout_vpll",
	.id		= -1,
	.ctrlbit	= (1 << 31),
};

/* Possible clock sources for APLL Mux */
static struct clk *clk_src_apll_list[] = {
	[0] = &clk_fin_apll,
	[1] = &clk_fout_apll,
};

struct clksrc_sources clk_src_apll = {
	.sources	= clk_src_apll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_apll_list),
};

/* Possible clock sources for BPLL Mux */
static struct clk *clk_src_bpll_list[] = {
	[0] = &clk_fin_bpll,
	[1] = &clk_fout_bpll,
};

struct clksrc_sources clk_src_bpll = {
	.sources	= clk_src_bpll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_bpll_list),
};

/* Possible clock sources for CPLL Mux */
static struct clk *clk_src_cpll_list[] = {
	[0] = &clk_fin_cpll,
	[1] = &clk_fout_cpll,
};

struct clksrc_sources clk_src_cpll = {
	.sources	= clk_src_cpll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_cpll_list),
};

/* Possible clock sources for MPLL Mux */
static struct clk *clk_src_mpll_list[] = {
	[0] = &clk_fin_mpll,
	[1] = &clk_fout_mpll,
};

struct clksrc_sources clk_src_mpll = {
	.sources	= clk_src_mpll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_mpll_list),
};

/* Possible clock sources for EPLL Mux */
static struct clk *clk_src_epll_list[] = {
	[0] = &clk_fin_epll,
	[1] = &clk_fout_epll,
};

struct clksrc_sources clk_src_epll = {
	.sources	= clk_src_epll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_epll_list),
};

/* Possible clock sources for DPLL Mux */
static struct clk *clk_src_dpll_list[] = {
	[0] = &clk_fin_dpll,
	[1] = &clk_fout_dpll,
};

struct clksrc_sources clk_src_dpll = {
	.sources	= clk_src_dpll_list,
	.nr_sources	= ARRAY_SIZE(clk_src_dpll_list),
};

struct clk clk_vpll = {
	.name		= "vpll",
	.id		= -1,
};

int s5p_gatectrl(void __iomem *reg, struct clk *clk, int enable)
{
	unsigned int ctrlbit = clk->ctrlbit;
	u32 con;

	con = __raw_readl(reg);
	con = enable ? (con | ctrlbit) : (con & ~ctrlbit);
	__raw_writel(con, reg);
	return 0;
}

int s5p_epll_enable(struct clk *clk, int enable)
{
	unsigned int ctrlbit = clk->ctrlbit;
	unsigned int epll_con = __raw_readl(S5P_EPLL_CON) & ~ctrlbit;

	if (enable)
		__raw_writel(epll_con | ctrlbit, S5P_EPLL_CON);
	else
		__raw_writel(epll_con, S5P_EPLL_CON);

	return 0;
}

unsigned long s5p_epll_get_rate(struct clk *clk)
{
	return clk->rate;
}

int s5p_spdif_set_rate(struct clk *clk, unsigned long rate)
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

unsigned long s5p_spdif_get_rate(struct clk *clk)
{
	struct clk *pclk;
	int rate;

	pclk = clk_get_parent(clk);
	if (IS_ERR(pclk))
		return -EINVAL;

	rate = pclk->ops->get_rate(pclk);
	clk_put(pclk);

	return rate;
}

struct clk_ops s5p_sclk_spdif_ops = {
	.set_rate	= s5p_spdif_set_rate,
	.get_rate	= s5p_spdif_get_rate,
};

static struct clk *s5p_clks[] __initdata = {
	&clk_ext_xtal_mux,
	&clk_48m,
	&s5p_clk_27m,
	&clk_fout_apll,
	&clk_fout_mpll,
	&clk_fout_epll,
	&clk_fout_dpll,
	&clk_fout_vpll,
	&clk_vpll,
	&clk_xusbxti,
};

void __init s5p_register_clocks(unsigned long xtal_freq)
{
	int ret;

	clk_ext_xtal_mux.rate = xtal_freq;

	ret = s3c24xx_register_clocks(s5p_clks, ARRAY_SIZE(s5p_clks));
	if (ret > 0)
		printk(KERN_ERR "Failed to register s5p clocks\n");
}
