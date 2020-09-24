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
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/mvebu-pmsu.h>
#include <asm/smp_plat.h>

#define SYS_CTRL_CLK_DIVIDER_CTRL_OFFSET               0x0
#define   SYS_CTRL_CLK_DIVIDER_CTRL_RESET_ALL          0xff
#define   SYS_CTRL_CLK_DIVIDER_CTRL_RESET_SHIFT        8
#define SYS_CTRL_CLK_DIVIDER_CTRL2_OFFSET              0x8
#define   SYS_CTRL_CLK_DIVIDER_CTRL2_NBCLK_RATIO_SHIFT 16
#define SYS_CTRL_CLK_DIVIDER_VALUE_OFFSET              0xC
#define SYS_CTRL_CLK_DIVIDER_MASK                      0x3F

#define PMU_DFS_RATIO_SHIFT 16
#define PMU_DFS_RATIO_MASK  0x3F

#define MAX_CPU	    4
struct cpu_clk {
	struct clk_hw hw;
	int cpu;
	const char *clk_name;
	const char *parent_name;
	void __iomem *reg_base;
	void __iomem *pmu_dfs;
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

static int clk_cpu_off_set_rate(struct clk_hw *hwclk, unsigned long rate,
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

static int clk_cpu_on_set_rate(struct clk_hw *hwclk, unsigned long rate,
			       unsigned long parent_rate)
{
	u32 reg;
	unsigned long fabric_div, target_div, cur_rate;
	struct cpu_clk *cpuclk = to_cpu_clk(hwclk);

	/*
	 * PMU DFS registers are not mapped, Device Tree does not
	 * describes them. We cannot change the frequency dynamically.
	 */
	if (!cpuclk->pmu_dfs)
		return -ENODEV;

	cur_rate = clk_hw_get_rate(hwclk);

	reg = readl(cpuclk->reg_base + SYS_CTRL_CLK_DIVIDER_CTRL2_OFFSET);
	fabric_div = (reg >> SYS_CTRL_CLK_DIVIDER_CTRL2_NBCLK_RATIO_SHIFT) &
		SYS_CTRL_CLK_DIVIDER_MASK;

	/* Frequency is going up */
	if (rate == 2 * cur_rate)
		target_div = fabric_div / 2;
	/* Frequency is going down */
	else
		target_div = fabric_div;

	if (target_div == 0)
		target_div = 1;

	reg = readl(cpuclk->pmu_dfs);
	reg &= ~(PMU_DFS_RATIO_MASK << PMU_DFS_RATIO_SHIFT);
	reg |= (target_div << PMU_DFS_RATIO_SHIFT);
	writel(reg, cpuclk->pmu_dfs);

	reg = readl(cpuclk->reg_base + SYS_CTRL_CLK_DIVIDER_CTRL_OFFSET);
	reg |= (SYS_CTRL_CLK_DIVIDER_CTRL_RESET_ALL <<
		SYS_CTRL_CLK_DIVIDER_CTRL_RESET_SHIFT);
	writel(reg, cpuclk->reg_base + SYS_CTRL_CLK_DIVIDER_CTRL_OFFSET);

	return mvebu_pmsu_dfs_request(cpuclk->cpu);
}

static int clk_cpu_set_rate(struct clk_hw *hwclk, unsigned long rate,
			    unsigned long parent_rate)
{
	if (__clk_is_enabled(hwclk->clk))
		return clk_cpu_on_set_rate(hwclk, rate, parent_rate);
	else
		return clk_cpu_off_set_rate(hwclk, rate, parent_rate);
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
	void __iomem *pmu_dfs_base = of_iomap(node, 1);
	int ncpus = 0;
	struct device_node *dn;

	if (clock_complex_base == NULL) {
		pr_err("%s: clock-complex base register not set\n",
			__func__);
		return;
	}

	if (pmu_dfs_base == NULL)
		pr_warn("%s: pmu-dfs base register not set, dynamic frequency scaling not available\n",
			__func__);

	for_each_node_by_type(dn, "cpu")
		ncpus++;

	cpuclk = kcalloc(ncpus, sizeof(*cpuclk), GFP_KERNEL);
	if (WARN_ON(!cpuclk))
		goto cpuclk_out;

	clks = kcalloc(ncpus, sizeof(*clks), GFP_KERNEL);
	if (WARN_ON(!clks))
		goto clks_out;

	for_each_node_by_type(dn, "cpu") {
		struct clk_init_data init = {};
		struct clk *clk;
		char *clk_name = kzalloc(5, GFP_KERNEL);
		int cpu, err;

		if (WARN_ON(!clk_name))
			goto bail_out;

		err = of_property_read_u32(dn, "reg", &cpu);
		if (WARN_ON(err))
			goto bail_out;

		sprintf(clk_name, "cpu%d", cpu);

		cpuclk[cpu].parent_name = of_clk_get_parent_name(node, 0);
		cpuclk[cpu].clk_name = clk_name;
		cpuclk[cpu].cpu = cpu;
		cpuclk[cpu].reg_base = clock_complex_base;
		if (pmu_dfs_base)
			cpuclk[cpu].pmu_dfs = pmu_dfs_base + 4 * cpu;
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

static void __init of_mv98dx3236_cpu_clk_setup(struct device_node *node)
{
	of_clk_add_provider(node, of_clk_src_simple_get, NULL);
}

CLK_OF_DECLARE(mv98dx3236_cpu_clock, "marvell,mv98dx3236-cpu-clock",
					 of_mv98dx3236_cpu_clk_setup);
