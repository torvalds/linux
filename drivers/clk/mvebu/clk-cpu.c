/*
 * Marvell MVEBU CPU clock handling.
 *
 * Copyright (C) 2012 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/kernel.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/delay.h>

#define SYS_CTRL_CLK_DIVIDER_CTRL_OFFSET    0x0
#define SYS_CTRL_CLK_DIVIDER_VALUE_OFFSET   0xC
#define SYS_CTRL_CLK_DIVIDER_MASK	    0x3F

#define MAX_CPU	    4
struct cpu_clk {
	struct clk_hw hw;
	int cpu;
	const char *clk_name;
	const char *parent_name;
	void __iomem *reg_base;
};

static struct clk **clks;

static struct clk_onecell_data clk_data;

#define to_cpu_clk(p) container_of(p, struct cpu_clk, hw)

static unsigned long clk_cpu_recalc_rate(struct clk_hw *hwclk,
					 unsigned long parent_rate)
{
	struct cpu_clk *cpuclk = to_cpu_clk(hwclk);
	u32 reg, div;

	reg = readl(cpuclk->reg_base + SYS_CTRL_CLK_DIVIDER_VALUE_OFFSET);
	div = (reg >> (cpuclk->cpu * 8)) & SYS_CTRL_CLK_DIVIDER_MASK;
	return parent_rate / div;
}

static long clk_cpu_round_rate(struct clk_hw *hwclk, unsigned long rate,
			       unsigned long *parent_rate)
{
	/* Valid ratio are 1:1, 1:2 and 1:3 */
	u32 div;

	div = *parent_rate / rate;
	if (div == 0)
		div = 1;
	else if (div > 3)
		div = 3;

	return *parent_rate / div;
}

static int clk_cpu_set_rate(struct clk_hw *hwclk, unsigned long rate,
			    unsigned long parent_rate)
{
	struct cpu_clk *cpuclk = to_cpu_clk(hwclk);
	u32 reg, div;
	u32 reload_mask;

	div = parent_rate / rate;
	reg = (readl(cpuclk->reg_base + SYS_CTRL_CLK_DIVIDER_VALUE_OFFSET)
		& (~(SYS_CTRL_CLK_DIVIDER_MASK << (cpuclk->cpu * 8))))
		| (div << (cpuclk->cpu * 8));
	writel(reg, cpuclk->reg_base + SYS_CTRL_CLK_DIVIDER_VALUE_OFFSET);
	/* Set clock divider reload smooth bit mask */
	reload_mask = 1 << (20 + cpuclk->cpu);

	reg = readl(cpuclk->reg_base + SYS_CTRL_CLK_DIVIDER_CTRL_OFFSET)
	    | reload_mask;
	writel(reg, cpuclk->reg_base + SYS_CTRL_CLK_DIVIDER_CTRL_OFFSET);

	/* Now trigger the clock update */
	reg = readl(cpuclk->reg_base + SYS_CTRL_CLK_DIVIDER_CTRL_OFFSET)
	    | 1 << 24;
	writel(reg, cpuclk->reg_base + SYS_CTRL_CLK_DIVIDER_CTRL_OFFSET);

	/* Wait for clocks to settle down then clear reload request */
	udelay(1000);
	reg &= ~(reload_mask | 1 << 24);
	writel(reg, cpuclk->reg_base + SYS_CTRL_CLK_DIVIDER_CTRL_OFFSET);
	udelay(1000);

	return 0;
}

static const struct clk_ops cpu_ops = {
	.recalc_rate = clk_cpu_recalc_rate,
	.round_rate = clk_cpu_round_rate,
	.set_rate = clk_cpu_set_rate,
};

static void __init of_cpu_clk_setup(struct device_node *node)
{
	struct cpu_clk *cpuclk;
	void __iomem *clock_complex_base = of_iomap(node, 0);
	int ncpus = 0;
	struct device_node *dn;

	if (clock_complex_base == NULL) {
		pr_err("%s: clock-complex base register not set\n",
			__func__);
		return;
	}

	for_each_node_by_type(dn, "cpu")
		ncpus++;

	cpuclk = kzalloc(ncpus * sizeof(*cpuclk), GFP_KERNEL);
	if (WARN_ON(!cpuclk))
		goto cpuclk_out;

	clks = kzalloc(ncpus * sizeof(*clks), GFP_KERNEL);
	if (WARN_ON(!clks))
		goto clks_out;

	for_each_node_by_type(dn, "cpu") {
		struct clk_init_data init;
		struct clk *clk;
		struct clk *parent_clk;
		char *clk_name = kzalloc(5, GFP_KERNEL);
		int cpu, err;

		if (WARN_ON(!clk_name))
			goto bail_out;

		err = of_property_read_u32(dn, "reg", &cpu);
		if (WARN_ON(err))
			goto bail_out;

		sprintf(clk_name, "cpu%d", cpu);
		parent_clk = of_clk_get(node, 0);

		cpuclk[cpu].parent_name = __clk_get_name(parent_clk);
		cpuclk[cpu].clk_name = clk_name;
		cpuclk[cpu].cpu = cpu;
		cpuclk[cpu].reg_base = clock_complex_base;
		cpuclk[cpu].hw.init = &init;

		init.name = cpuclk[cpu].clk_name;
		init.ops = &cpu_ops;
		init.flags = 0;
		init.parent_names = &cpuclk[cpu].parent_name;
		init.num_parents = 1;

		clk = clk_register(NULL, &cpuclk[cpu].hw);
		if (WARN_ON(IS_ERR(clk)))
			goto bail_out;
		clks[cpu] = clk;
	}
	clk_data.clk_num = MAX_CPU;
	clk_data.clks = clks;
	of_clk_add_provider(node, of_clk_src_onecell_get, &clk_data);

	return;
bail_out:
	kfree(clks);
	while(ncpus--)
		kfree(cpuclk[ncpus].clk_name);
clks_out:
	kfree(cpuclk);
cpuclk_out:
	iounmap(clock_complex_base);
}

CLK_OF_DECLARE(armada_xp_cpu_clock, "marvell,armada-xp-cpu-clock",
					 of_cpu_clk_setup);
