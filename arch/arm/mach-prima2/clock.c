/*
 * Clock tree for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/mach/map.h>
#include <mach/map.h>

#define SIRFSOC_CLKC_CLK_EN0    0x0000
#define SIRFSOC_CLKC_CLK_EN1    0x0004
#define SIRFSOC_CLKC_REF_CFG    0x0014
#define SIRFSOC_CLKC_CPU_CFG    0x0018
#define SIRFSOC_CLKC_MEM_CFG    0x001c
#define SIRFSOC_CLKC_SYS_CFG    0x0020
#define SIRFSOC_CLKC_IO_CFG     0x0024
#define SIRFSOC_CLKC_DSP_CFG    0x0028
#define SIRFSOC_CLKC_GFX_CFG    0x002c
#define SIRFSOC_CLKC_MM_CFG     0x0030
#define SIRFSOC_LKC_LCD_CFG     0x0034
#define SIRFSOC_CLKC_MMC_CFG    0x0038
#define SIRFSOC_CLKC_PLL1_CFG0  0x0040
#define SIRFSOC_CLKC_PLL2_CFG0  0x0044
#define SIRFSOC_CLKC_PLL3_CFG0  0x0048
#define SIRFSOC_CLKC_PLL1_CFG1  0x004c
#define SIRFSOC_CLKC_PLL2_CFG1  0x0050
#define SIRFSOC_CLKC_PLL3_CFG1  0x0054
#define SIRFSOC_CLKC_PLL1_CFG2  0x0058
#define SIRFSOC_CLKC_PLL2_CFG2  0x005c
#define SIRFSOC_CLKC_PLL3_CFG2  0x0060

#define SIRFSOC_CLOCK_VA_BASE		SIRFSOC_VA(0x005000)

#define KHZ     1000
#define MHZ     (KHZ * KHZ)

struct clk_ops {
	unsigned long (*get_rate)(struct clk *clk);
	long (*round_rate)(struct clk *clk, unsigned long rate);
	int (*set_rate)(struct clk *clk, unsigned long rate);
	int (*enable)(struct clk *clk);
	int (*disable)(struct clk *clk);
	struct clk *(*get_parent)(struct clk *clk);
	int (*set_parent)(struct clk *clk, struct clk *parent);
};

struct clk {
	struct clk *parent;     /* parent clk */
	unsigned long rate;     /* clock rate in Hz */
	signed char usage;      /* clock enable count */
	signed char enable_bit; /* enable bit: 0 ~ 63 */
	unsigned short regofs;  /* register offset */
	struct clk_ops *ops;    /* clock operation */
};

static DEFINE_SPINLOCK(clocks_lock);

static inline unsigned long clkc_readl(unsigned reg)
{
	return readl(SIRFSOC_CLOCK_VA_BASE + reg);
}

static inline void clkc_writel(u32 val, unsigned reg)
{
	writel(val, SIRFSOC_CLOCK_VA_BASE + reg);
}

/*
 * osc_rtc - real time oscillator - 32.768KHz
 * osc_sys - high speed oscillator - 26MHz
 */

static struct clk clk_rtc = {
	.rate = 32768,
};

static struct clk clk_osc = {
	.rate = 26 * MHZ,
};

/*
 * std pll
 */
static unsigned long std_pll_get_rate(struct clk *clk)
{
	unsigned long fin = clk_get_rate(clk->parent);
	u32 regcfg2 = clk->regofs + SIRFSOC_CLKC_PLL1_CFG2 -
		SIRFSOC_CLKC_PLL1_CFG0;

	if (clkc_readl(regcfg2) & BIT(2)) {
		/* pll bypass mode */
		clk->rate = fin;
	} else {
		/* fout = fin * nf / nr / od */
		u32 cfg0 = clkc_readl(clk->regofs);
		u32 nf = (cfg0 & (BIT(13) - 1)) + 1;
		u32 nr = ((cfg0 >> 13) & (BIT(6) - 1)) + 1;
		u32 od = ((cfg0 >> 19) & (BIT(4) - 1)) + 1;
		WARN_ON(fin % MHZ);
		clk->rate = fin / MHZ * nf / nr / od * MHZ;
	}

	return clk->rate;
}

static int std_pll_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long fin, nf, nr, od, reg;

	/*
	 * fout = fin * nf / (nr * od);
	 * set od = 1, nr = fin/MHz, so fout = nf * MHz
	 */

	nf = rate / MHZ;
	if (unlikely((rate % MHZ) || nf > BIT(13) || nf < 1))
		return -EINVAL;

	fin = clk_get_rate(clk->parent);
	BUG_ON(fin < MHZ);

	nr = fin / MHZ;
	BUG_ON((fin % MHZ) || nr > BIT(6));

	od = 1;

	reg = (nf - 1) | ((nr - 1) << 13) | ((od - 1) << 19);
	clkc_writel(reg, clk->regofs);

	reg = clk->regofs + SIRFSOC_CLKC_PLL1_CFG1 - SIRFSOC_CLKC_PLL1_CFG0;
	clkc_writel((nf >> 1) - 1, reg);

	reg = clk->regofs + SIRFSOC_CLKC_PLL1_CFG2 - SIRFSOC_CLKC_PLL1_CFG0;
	while (!(clkc_readl(reg) & BIT(6)))
		cpu_relax();

	clk->rate = 0; /* set to zero will force recalculation */
	return 0;
}

static struct clk_ops std_pll_ops = {
	.get_rate = std_pll_get_rate,
	.set_rate = std_pll_set_rate,
};

static struct clk clk_pll1 = {
	.parent = &clk_osc,
	.regofs = SIRFSOC_CLKC_PLL1_CFG0,
	.ops = &std_pll_ops,
};

static struct clk clk_pll2 = {
	.parent = &clk_osc,
	.regofs = SIRFSOC_CLKC_PLL2_CFG0,
	.ops = &std_pll_ops,
};

static struct clk clk_pll3 = {
	.parent = &clk_osc,
	.regofs = SIRFSOC_CLKC_PLL3_CFG0,
	.ops = &std_pll_ops,
};

/*
 * clock domains - cpu, mem, sys/io
 */

static struct clk clk_mem;

static struct clk *dmn_get_parent(struct clk *clk)
{
	struct clk *clks[] = {
		&clk_osc, &clk_rtc, &clk_pll1, &clk_pll2, &clk_pll3
	};
	u32 cfg = clkc_readl(clk->regofs);
	WARN_ON((cfg & (BIT(3) - 1)) > 4);
	return clks[cfg & (BIT(3) - 1)];
}

static int dmn_set_parent(struct clk *clk, struct clk *parent)
{
	const struct clk *clks[] = {
		&clk_osc, &clk_rtc, &clk_pll1, &clk_pll2, &clk_pll3
	};
	u32 cfg = clkc_readl(clk->regofs);
	int i;
	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		if (clks[i] == parent) {
			cfg &= ~(BIT(3) - 1);
			clkc_writel(cfg | i, clk->regofs);
			/* BIT(3) - switching status: 1 - busy, 0 - done */
			while (clkc_readl(clk->regofs) & BIT(3))
				cpu_relax();
			return 0;
		}
	}
	return -EINVAL;
}

static unsigned long dmn_get_rate(struct clk *clk)
{
	unsigned long fin = clk_get_rate(clk->parent);
	u32 cfg = clkc_readl(clk->regofs);
	if (cfg & BIT(24)) {
		/* fcd bypass mode */
		clk->rate = fin;
	} else {
		/*
		 * wait count: bit[19:16], hold count: bit[23:20]
		 */
		u32 wait = (cfg >> 16) & (BIT(4) - 1);
		u32 hold = (cfg >> 20) & (BIT(4) - 1);

		clk->rate = fin / (wait + hold + 2);
	}

	return clk->rate;
}

static int dmn_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long fin;
	unsigned ratio, wait, hold, reg;
	unsigned bits = (clk == &clk_mem) ? 3 : 4;

	fin = clk_get_rate(clk->parent);
	ratio = fin / rate;

	if (unlikely(ratio < 2 || ratio > BIT(bits + 1)))
		return -EINVAL;

	WARN_ON(fin % rate);

	wait = (ratio >> 1) - 1;
	hold = ratio - wait - 2;

	reg = clkc_readl(clk->regofs);
	reg &= ~(((BIT(bits) - 1) << 16) | ((BIT(bits) - 1) << 20));
	reg |= (wait << 16) | (hold << 20) | BIT(25);
	clkc_writel(reg, clk->regofs);

	/* waiting FCD been effective */
	while (clkc_readl(clk->regofs) & BIT(25))
		cpu_relax();

	clk->rate = 0; /* set to zero will force recalculation */

	return 0;
}

/*
 * cpu clock has no FCD register in Prima2, can only change pll
 */
static int cpu_set_rate(struct clk *clk, unsigned long rate)
{
	int ret1, ret2;
	struct clk *cur_parent, *tmp_parent;

	cur_parent = dmn_get_parent(clk);
	BUG_ON(cur_parent == NULL || cur_parent->usage > 1);

	/* switch to tmp pll before setting parent clock's rate */
	tmp_parent = cur_parent == &clk_pll1 ? &clk_pll2 : &clk_pll1;
	ret1 = dmn_set_parent(clk, tmp_parent);
	BUG_ON(ret1);

	ret2 = clk_set_rate(cur_parent, rate);

	ret1 = dmn_set_parent(clk, cur_parent);

	clk->rate = 0; /* set to zero will force recalculation */

	return ret2 ? ret2 : ret1;
}

static struct clk_ops cpu_ops = {
	.get_parent = dmn_get_parent,
	.set_parent = dmn_set_parent,
	.set_rate = cpu_set_rate,
};

static struct clk clk_cpu = {
	.parent = &clk_pll1,
	.regofs = SIRFSOC_CLKC_CPU_CFG,
	.ops = &cpu_ops,
};


static struct clk_ops msi_ops = {
	.set_rate = dmn_set_rate,
	.get_rate = dmn_get_rate,
	.set_parent = dmn_set_parent,
	.get_parent = dmn_get_parent,
};

static struct clk clk_mem = {
	.parent = &clk_pll2,
	.regofs = SIRFSOC_CLKC_MEM_CFG,
	.ops = &msi_ops,
};

static struct clk clk_sys = {
	.parent = &clk_pll3,
	.regofs = SIRFSOC_CLKC_SYS_CFG,
	.ops = &msi_ops,
};

static struct clk clk_io = {
	.parent = &clk_pll3,
	.regofs = SIRFSOC_CLKC_IO_CFG,
	.ops = &msi_ops,
};

/*
 * on-chip clock sets
 */
static struct clk_lookup onchip_clks[] = {
	{
		.dev_id = "rtc",
		.clk = &clk_rtc,
	}, {
		.dev_id = "osc",
		.clk = &clk_osc,
	}, {
		.dev_id = "pll1",
		.clk = &clk_pll1,
	}, {
		.dev_id = "pll2",
		.clk = &clk_pll2,
	}, {
		.dev_id = "pll3",
		.clk = &clk_pll3,
	}, {
		.dev_id = "cpu",
		.clk = &clk_cpu,
	}, {
		.dev_id = "mem",
		.clk = &clk_mem,
	}, {
		.dev_id = "sys",
			.clk = &clk_sys,
	}, {
		.dev_id = "io",
			.clk = &clk_io,
	},
};

int clk_enable(struct clk *clk)
{
	unsigned long flags;

	if (unlikely(IS_ERR_OR_NULL(clk)))
		return -EINVAL;

	if (clk->parent)
		clk_enable(clk->parent);

	spin_lock_irqsave(&clocks_lock, flags);
	if (!clk->usage++ && clk->ops && clk->ops->enable)
		clk->ops->enable(clk);
	spin_unlock_irqrestore(&clocks_lock, flags);
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (unlikely(IS_ERR_OR_NULL(clk)))
		return;

	WARN_ON(!clk->usage);

	spin_lock_irqsave(&clocks_lock, flags);
	if (--clk->usage == 0 && clk->ops && clk->ops->disable)
		clk->ops->disable(clk);
	spin_unlock_irqrestore(&clocks_lock, flags);

	if (clk->parent)
		clk_disable(clk->parent);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	if (unlikely(IS_ERR_OR_NULL(clk)))
		return 0;

	if (clk->rate)
		return clk->rate;

	if (clk->ops && clk->ops->get_rate)
		return clk->ops->get_rate(clk);

	return clk_get_rate(clk->parent);
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (unlikely(IS_ERR_OR_NULL(clk)))
		return 0;

	if (clk->ops && clk->ops->round_rate)
		return clk->ops->round_rate(clk, rate);

	return 0;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	if (unlikely(IS_ERR_OR_NULL(clk)))
		return -EINVAL;

	if (!clk->ops || !clk->ops->set_rate)
		return -EINVAL;

	return clk->ops->set_rate(clk, rate);
}
EXPORT_SYMBOL(clk_set_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	int ret;
	unsigned long flags;

	if (unlikely(IS_ERR_OR_NULL(clk)))
		return -EINVAL;

	if (!clk->ops || !clk->ops->set_parent)
		return -EINVAL;

	spin_lock_irqsave(&clocks_lock, flags);
	ret = clk->ops->set_parent(clk, parent);
	if (!ret) {
		parent->usage += clk->usage;
		clk->parent->usage -= clk->usage;
		BUG_ON(clk->parent->usage < 0);
		clk->parent = parent;
	}
	spin_unlock_irqrestore(&clocks_lock, flags);
	return ret;
}
EXPORT_SYMBOL(clk_set_parent);

struct clk *clk_get_parent(struct clk *clk)
{
	unsigned long flags;

	if (unlikely(IS_ERR_OR_NULL(clk)))
		return NULL;

	if (!clk->ops || !clk->ops->get_parent)
		return clk->parent;

	spin_lock_irqsave(&clocks_lock, flags);
	clk->parent = clk->ops->get_parent(clk);
	spin_unlock_irqrestore(&clocks_lock, flags);
	return clk->parent;
}
EXPORT_SYMBOL(clk_get_parent);

static void __init sirfsoc_clk_init(void)
{
	clkdev_add_table(onchip_clks, ARRAY_SIZE(onchip_clks));
}

static struct of_device_id clkc_ids[] = {
	{ .compatible = "sirf,prima2-clkc" },
};

void __init sirfsoc_of_clk_init(void)
{
	struct device_node *np;
	struct resource res;
	struct map_desc sirfsoc_clkc_iodesc = {
		.virtual = SIRFSOC_CLOCK_VA_BASE,
		.type    = MT_DEVICE,
	};

	np = of_find_matching_node(NULL, clkc_ids);
	if (!np)
		panic("unable to find compatible clkc node in dtb\n");

	if (of_address_to_resource(np, 0, &res))
		panic("unable to find clkc range in dtb");
	of_node_put(np);

	sirfsoc_clkc_iodesc.pfn = __phys_to_pfn(res.start);
	sirfsoc_clkc_iodesc.length = 1 + res.end - res.start;

	iotable_init(&sirfsoc_clkc_iodesc, 1);

	sirfsoc_clk_init();
}
