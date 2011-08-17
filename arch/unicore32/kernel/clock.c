/*
 * linux/arch/unicore32/kernel/clock.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <mach/hardware.h>

/*
 * Very simple clock implementation
 */
struct clk {
	struct list_head	node;
	unsigned long		rate;
	const char		*name;
};

static struct clk clk_ost_clk = {
	.name		= "OST_CLK",
	.rate		= CLOCK_TICK_RATE,
};

static struct clk clk_mclk_clk = {
	.name		= "MAIN_CLK",
};

static struct clk clk_bclk32_clk = {
	.name		= "BUS32_CLK",
};

static struct clk clk_ddr_clk = {
	.name		= "DDR_CLK",
};

static struct clk clk_vga_clk = {
	.name		= "VGA_CLK",
};

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);

	mutex_lock(&clocks_mutex);
	list_for_each_entry(p, &clocks, node) {
		if (strcmp(id, p->name) == 0) {
			clk = p;
			break;
		}
	}
	mutex_unlock(&clocks_mutex);

	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

int clk_enable(struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

struct {
	unsigned long rate;
	unsigned long cfg;
	unsigned long div;
} vga_clk_table[] = {
	{.rate =  25175000, .cfg = 0x00002001, .div = 0x9},
	{.rate =  31500000, .cfg = 0x00002001, .div = 0x7},
	{.rate =  40000000, .cfg = 0x00003801, .div = 0x9},
	{.rate =  49500000, .cfg = 0x00003801, .div = 0x7},
	{.rate =  65000000, .cfg = 0x00002c01, .div = 0x4},
	{.rate =  78750000, .cfg = 0x00002400, .div = 0x7},
	{.rate = 108000000, .cfg = 0x00002c01, .div = 0x2},
	{.rate = 106500000, .cfg = 0x00003c01, .div = 0x3},
	{.rate =  50650000, .cfg = 0x00106400, .div = 0x9},
	{.rate =  61500000, .cfg = 0x00106400, .div = 0xa},
	{.rate =  85500000, .cfg = 0x00002800, .div = 0x6},
};

struct {
	unsigned long mrate;
	unsigned long prate;
} mclk_clk_table[] = {
	{.mrate = 500000000, .prate = 0x00109801},
	{.mrate = 525000000, .prate = 0x00104C00},
	{.mrate = 550000000, .prate = 0x00105000},
	{.mrate = 575000000, .prate = 0x00105400},
	{.mrate = 600000000, .prate = 0x00105800},
	{.mrate = 625000000, .prate = 0x00105C00},
	{.mrate = 650000000, .prate = 0x00106000},
	{.mrate = 675000000, .prate = 0x00106400},
	{.mrate = 700000000, .prate = 0x00106800},
	{.mrate = 725000000, .prate = 0x00106C00},
	{.mrate = 750000000, .prate = 0x00107000},
	{.mrate = 775000000, .prate = 0x00107400},
	{.mrate = 800000000, .prate = 0x00107800},
};

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	if (clk == &clk_vga_clk) {
		unsigned long pll_vgacfg, pll_vgadiv;
		int ret, i;

		/* lookup vga_clk_table */
		ret = -EINVAL;
		for (i = 0; i < ARRAY_SIZE(vga_clk_table); i++) {
			if (rate == vga_clk_table[i].rate) {
				pll_vgacfg = vga_clk_table[i].cfg;
				pll_vgadiv = vga_clk_table[i].div;
				ret = 0;
				break;
			}
		}

		if (ret)
			return ret;

		if (readl(PM_PLLVGACFG) == pll_vgacfg)
			return 0;

		/* set pll vga cfg reg. */
		writel(pll_vgacfg, PM_PLLVGACFG);

		writel(PM_PMCR_CFBVGA, PM_PMCR);
		while ((readl(PM_PLLDFCDONE) & PM_PLLDFCDONE_VGADFC)
				!= PM_PLLDFCDONE_VGADFC)
			udelay(100); /* about 1ms */

		/* set div cfg reg. */
		writel(readl(PM_PCGR) | PM_PCGR_VGACLK, PM_PCGR);

		writel((readl(PM_DIVCFG) & ~PM_DIVCFG_VGACLK_MASK)
				| PM_DIVCFG_VGACLK(pll_vgadiv), PM_DIVCFG);

		writel(readl(PM_SWRESET) | PM_SWRESET_VGADIV, PM_SWRESET);
		while ((readl(PM_SWRESET) & PM_SWRESET_VGADIV)
				== PM_SWRESET_VGADIV)
			udelay(100); /* 65536 bclk32, about 320us */

		writel(readl(PM_PCGR) & ~PM_PCGR_VGACLK, PM_PCGR);
	}
#ifdef CONFIG_CPU_FREQ
	if (clk == &clk_mclk_clk) {
		u32 pll_rate, divstatus = PM_DIVSTATUS;
		int ret, i;

		/* lookup mclk_clk_table */
		ret = -EINVAL;
		for (i = 0; i < ARRAY_SIZE(mclk_clk_table); i++) {
			if (rate == mclk_clk_table[i].mrate) {
				pll_rate = mclk_clk_table[i].prate;
				clk_mclk_clk.rate = mclk_clk_table[i].mrate;
				ret = 0;
				break;
			}
		}

		if (ret)
			return ret;

		if (clk_mclk_clk.rate)
			clk_bclk32_clk.rate = clk_mclk_clk.rate
				/ (((divstatus & 0x0000f000) >> 12) + 1);

		/* set pll sys cfg reg. */
		PM_PLLSYSCFG = pll_rate;

		PM_PMCR = PM_PMCR_CFBSYS;
		while ((PM_PLLDFCDONE & PM_PLLDFCDONE_SYSDFC)
				!= PM_PLLDFCDONE_SYSDFC)
			udelay(100);
			/* about 1ms */
	}
#endif
	return 0;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_register(struct clk *clk)
{
	mutex_lock(&clocks_mutex);
	list_add(&clk->node, &clocks);
	mutex_unlock(&clocks_mutex);
	printk(KERN_DEFAULT "PKUnity PM: %s %lu.%02luM\n", clk->name,
		(clk->rate)/1000000, (clk->rate)/10000 % 100);
	return 0;
}
EXPORT_SYMBOL(clk_register);

void clk_unregister(struct clk *clk)
{
	mutex_lock(&clocks_mutex);
	list_del(&clk->node);
	mutex_unlock(&clocks_mutex);
}
EXPORT_SYMBOL(clk_unregister);

struct {
	unsigned long prate;
	unsigned long rate;
} pllrate_table[] = {
	{.prate = 0x00002001, .rate = 250000000},
	{.prate = 0x00104801, .rate = 250000000},
	{.prate = 0x00104C01, .rate = 262500000},
	{.prate = 0x00002401, .rate = 275000000},
	{.prate = 0x00105001, .rate = 275000000},
	{.prate = 0x00105401, .rate = 287500000},
	{.prate = 0x00002801, .rate = 300000000},
	{.prate = 0x00105801, .rate = 300000000},
	{.prate = 0x00105C01, .rate = 312500000},
	{.prate = 0x00002C01, .rate = 325000000},
	{.prate = 0x00106001, .rate = 325000000},
	{.prate = 0x00106401, .rate = 337500000},
	{.prate = 0x00003001, .rate = 350000000},
	{.prate = 0x00106801, .rate = 350000000},
	{.prate = 0x00106C01, .rate = 362500000},
	{.prate = 0x00003401, .rate = 375000000},
	{.prate = 0x00107001, .rate = 375000000},
	{.prate = 0x00107401, .rate = 387500000},
	{.prate = 0x00003801, .rate = 400000000},
	{.prate = 0x00107801, .rate = 400000000},
	{.prate = 0x00107C01, .rate = 412500000},
	{.prate = 0x00003C01, .rate = 425000000},
	{.prate = 0x00108001, .rate = 425000000},
	{.prate = 0x00108401, .rate = 437500000},
	{.prate = 0x00004001, .rate = 450000000},
	{.prate = 0x00108801, .rate = 450000000},
	{.prate = 0x00108C01, .rate = 462500000},
	{.prate = 0x00004401, .rate = 475000000},
	{.prate = 0x00109001, .rate = 475000000},
	{.prate = 0x00109401, .rate = 487500000},
	{.prate = 0x00004801, .rate = 500000000},
	{.prate = 0x00109801, .rate = 500000000},
	{.prate = 0x00104C00, .rate = 525000000},
	{.prate = 0x00002400, .rate = 550000000},
	{.prate = 0x00105000, .rate = 550000000},
	{.prate = 0x00105400, .rate = 575000000},
	{.prate = 0x00002800, .rate = 600000000},
	{.prate = 0x00105800, .rate = 600000000},
	{.prate = 0x00105C00, .rate = 625000000},
	{.prate = 0x00002C00, .rate = 650000000},
	{.prate = 0x00106000, .rate = 650000000},
	{.prate = 0x00106400, .rate = 675000000},
	{.prate = 0x00003000, .rate = 700000000},
	{.prate = 0x00106800, .rate = 700000000},
	{.prate = 0x00106C00, .rate = 725000000},
	{.prate = 0x00003400, .rate = 750000000},
	{.prate = 0x00107000, .rate = 750000000},
	{.prate = 0x00107400, .rate = 775000000},
	{.prate = 0x00003800, .rate = 800000000},
	{.prate = 0x00107800, .rate = 800000000},
	{.prate = 0x00107C00, .rate = 825000000},
	{.prate = 0x00003C00, .rate = 850000000},
	{.prate = 0x00108000, .rate = 850000000},
	{.prate = 0x00108400, .rate = 875000000},
	{.prate = 0x00004000, .rate = 900000000},
	{.prate = 0x00108800, .rate = 900000000},
	{.prate = 0x00108C00, .rate = 925000000},
	{.prate = 0x00004400, .rate = 950000000},
	{.prate = 0x00109000, .rate = 950000000},
	{.prate = 0x00109400, .rate = 975000000},
	{.prate = 0x00004800, .rate = 1000000000},
	{.prate = 0x00109800, .rate = 1000000000},
};

struct {
	unsigned long prate;
	unsigned long drate;
} pddr_table[] = {
	{.prate = 0x00100800, .drate = 44236800},
	{.prate = 0x00100C00, .drate = 66355200},
	{.prate = 0x00101000, .drate = 88473600},
	{.prate = 0x00101400, .drate = 110592000},
	{.prate = 0x00101800, .drate = 132710400},
	{.prate = 0x00101C01, .drate = 154828800},
	{.prate = 0x00102001, .drate = 176947200},
	{.prate = 0x00102401, .drate = 199065600},
	{.prate = 0x00102801, .drate = 221184000},
	{.prate = 0x00102C01, .drate = 243302400},
	{.prate = 0x00103001, .drate = 265420800},
	{.prate = 0x00103401, .drate = 287539200},
	{.prate = 0x00103801, .drate = 309657600},
	{.prate = 0x00103C01, .drate = 331776000},
	{.prate = 0x00104001, .drate = 353894400},
};

static int __init clk_init(void)
{
#ifdef CONFIG_PUV3_PM
	u32 pllrate, divstatus = readl(PM_DIVSTATUS);
	u32 pcgr_val = readl(PM_PCGR);
	int i;

	pcgr_val |= PM_PCGR_BCLKMME | PM_PCGR_BCLKH264E | PM_PCGR_BCLKH264D
			| PM_PCGR_HECLK | PM_PCGR_HDCLK;
	writel(pcgr_val, PM_PCGR);

	pllrate = readl(PM_PLLSYSSTATUS);

	/* lookup pmclk_table */
	clk_mclk_clk.rate = 0;
	for (i = 0; i < ARRAY_SIZE(pllrate_table); i++) {
		if (pllrate == pllrate_table[i].prate) {
			clk_mclk_clk.rate = pllrate_table[i].rate;
			break;
		}
	}

	if (clk_mclk_clk.rate)
		clk_bclk32_clk.rate = clk_mclk_clk.rate /
			(((divstatus & 0x0000f000) >> 12) + 1);

	pllrate = readl(PM_PLLDDRSTATUS);

	/* lookup pddr_table */
	clk_ddr_clk.rate = 0;
	for (i = 0; i < ARRAY_SIZE(pddr_table); i++) {
		if (pllrate == pddr_table[i].prate) {
			clk_ddr_clk.rate = pddr_table[i].drate;
			break;
		}
	}

	pllrate = readl(PM_PLLVGASTATUS);

	/* lookup pvga_table */
	clk_vga_clk.rate = 0;
	for (i = 0; i < ARRAY_SIZE(pllrate_table); i++) {
		if (pllrate == pllrate_table[i].prate) {
			clk_vga_clk.rate = pllrate_table[i].rate;
			break;
		}
	}

	if (clk_vga_clk.rate)
		clk_vga_clk.rate = clk_vga_clk.rate /
			(((divstatus & 0x00f00000) >> 20) + 1);

	clk_register(&clk_vga_clk);
#endif
#ifdef CONFIG_ARCH_FPGA
	clk_ddr_clk.rate = 33000000;
	clk_mclk_clk.rate = 33000000;
	clk_bclk32_clk.rate = 33000000;
#endif
	clk_register(&clk_ddr_clk);
	clk_register(&clk_mclk_clk);
	clk_register(&clk_bclk32_clk);
	clk_register(&clk_ost_clk);
	return 0;
}
core_initcall(clk_init);
