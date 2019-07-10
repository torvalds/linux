// SPDX-License-Identifier: GPL-2.0+
/*
 * Marvell Armada AP CPU Clock Controller
 *
 * Copyright (C) 2018 Marvell
 *
 * Omri Itach <omrii@marvell.com>
 * Gregory Clement <gregory.clement@bootlin.com>
 */

#define pr_fmt(fmt) "ap-cpu-clk: " fmt

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include "armada_ap_cp_helper.h"

#define AP806_CPU_CLUSTER0		0
#define AP806_CPU_CLUSTER1		1
#define AP806_CPUS_PER_CLUSTER		2
#define APN806_CPU1_MASK		0x1

#define APN806_CLUSTER_NUM_OFFSET	8
#define APN806_CLUSTER_NUM_MASK		BIT(APN806_CLUSTER_NUM_OFFSET)

#define APN806_MAX_DIVIDER		32

/* AP806 CPU DFS register mapping*/
#define AP806_CA72MP2_0_PLL_CR_0_REG_OFFSET		0x278
#define AP806_CA72MP2_0_PLL_CR_1_REG_OFFSET		0x280
#define AP806_CA72MP2_0_PLL_CR_2_REG_OFFSET		0x284
#define AP806_CA72MP2_0_PLL_SR_REG_OFFSET		0xC94

#define AP806_CA72MP2_0_PLL_CR_CLUSTER_OFFSET		0x14
#define AP806_PLL_CR_0_CPU_CLK_DIV_RATIO_OFFSET		0
#define AP806_PLL_CR_0_CPU_CLK_DIV_RATIO_MASK \
			(0x3f << AP806_PLL_CR_0_CPU_CLK_DIV_RATIO_OFFSET)
#define AP806_PLL_CR_0_CPU_CLK_RELOAD_FORCE_OFFSET	24
#define AP806_PLL_CR_0_CPU_CLK_RELOAD_FORCE_MASK \
			(0x1 << AP806_PLL_CR_0_CPU_CLK_RELOAD_FORCE_OFFSET)
#define AP806_PLL_CR_0_CPU_CLK_RELOAD_RATIO_OFFSET	16
#define AP806_CA72MP2_0_PLL_RATIO_STATE			11

#define STATUS_POLL_PERIOD_US		1
#define STATUS_POLL_TIMEOUT_US		1000000

#define to_ap_cpu_clk(_hw) container_of(_hw, struct ap_cpu_clk, hw)

/*
 * struct ap806_clk: CPU cluster clock controller instance
 * @cluster: Cluster clock controller index
 * @clk_name: Cluster clock controller name
 * @dev : Cluster clock device
 * @hw: HW specific structure of Cluster clock controller
 * @pll_cr_base: CA72MP2 Register base (Device Sample at Reset register)
 */
struct ap_cpu_clk {
	unsigned int cluster;
	const char *clk_name;
	struct device *dev;
	struct clk_hw hw;
	struct regmap *pll_cr_base;
};

static unsigned long ap_cpu_clk_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct ap_cpu_clk *clk = to_ap_cpu_clk(hw);
	unsigned int cpu_clkdiv_reg;
	int cpu_clkdiv_ratio;

	cpu_clkdiv_reg = AP806_CA72MP2_0_PLL_CR_0_REG_OFFSET +
		(clk->cluster * AP806_CA72MP2_0_PLL_CR_CLUSTER_OFFSET);
	regmap_read(clk->pll_cr_base, cpu_clkdiv_reg, &cpu_clkdiv_ratio);
	cpu_clkdiv_ratio &= AP806_PLL_CR_0_CPU_CLK_DIV_RATIO_MASK;
	cpu_clkdiv_ratio >>= AP806_PLL_CR_0_CPU_CLK_DIV_RATIO_OFFSET;

	return parent_rate / cpu_clkdiv_ratio;
}

static int ap_cpu_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct ap_cpu_clk *clk = to_ap_cpu_clk(hw);
	int ret, reg, divider = parent_rate / rate;
	unsigned int cpu_clkdiv_reg, cpu_force_reg, cpu_ratio_reg, stable_bit;

	cpu_clkdiv_reg = AP806_CA72MP2_0_PLL_CR_0_REG_OFFSET +
		(clk->cluster * AP806_CA72MP2_0_PLL_CR_CLUSTER_OFFSET);
	cpu_force_reg = AP806_CA72MP2_0_PLL_CR_1_REG_OFFSET +
		(clk->cluster * AP806_CA72MP2_0_PLL_CR_CLUSTER_OFFSET);
	cpu_ratio_reg = AP806_CA72MP2_0_PLL_CR_2_REG_OFFSET +
		(clk->cluster * AP806_CA72MP2_0_PLL_CR_CLUSTER_OFFSET);

	regmap_update_bits(clk->pll_cr_base, cpu_clkdiv_reg,
			   AP806_PLL_CR_0_CPU_CLK_DIV_RATIO_MASK, divider);

	regmap_update_bits(clk->pll_cr_base, cpu_force_reg,
			   AP806_PLL_CR_0_CPU_CLK_RELOAD_FORCE_MASK,
			   AP806_PLL_CR_0_CPU_CLK_RELOAD_FORCE_MASK);

	regmap_update_bits(clk->pll_cr_base, cpu_ratio_reg,
			   BIT(AP806_PLL_CR_0_CPU_CLK_RELOAD_RATIO_OFFSET),
			   BIT(AP806_PLL_CR_0_CPU_CLK_RELOAD_RATIO_OFFSET));

	stable_bit = BIT(clk->cluster * AP806_CA72MP2_0_PLL_RATIO_STATE),

	ret = regmap_read_poll_timeout(clk->pll_cr_base,
				       AP806_CA72MP2_0_PLL_SR_REG_OFFSET, reg,
				       reg & stable_bit, STATUS_POLL_PERIOD_US,
				       STATUS_POLL_TIMEOUT_US);
	if (ret)
		return ret;

	regmap_update_bits(clk->pll_cr_base, cpu_ratio_reg,
			   BIT(AP806_PLL_CR_0_CPU_CLK_RELOAD_RATIO_OFFSET), 0);

	return 0;
}

static long ap_cpu_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *parent_rate)
{
	int divider = *parent_rate / rate;

	divider = min(divider, APN806_MAX_DIVIDER);

	return *parent_rate / divider;
}

static const struct clk_ops ap_cpu_clk_ops = {
	.recalc_rate	= ap_cpu_clk_recalc_rate,
	.round_rate	= ap_cpu_clk_round_rate,
	.set_rate	= ap_cpu_clk_set_rate,
};

static int ap_cpu_clock_probe(struct platform_device *pdev)
{
	int ret, nclusters = 0, cluster_index = 0;
	struct device *dev = &pdev->dev;
	struct device_node *dn, *np = dev->of_node;
	struct clk_hw_onecell_data *ap_cpu_data;
	struct ap_cpu_clk *ap_cpu_clk;
	struct regmap *regmap;

	regmap = syscon_node_to_regmap(np->parent);
	if (IS_ERR(regmap)) {
		pr_err("cannot get pll_cr_base regmap\n");
		return PTR_ERR(regmap);
	}

	/*
	 * AP806 has 4 cpus and DFS for AP806 is controlled per
	 * cluster (2 CPUs per cluster), cpu0 and cpu1 are fixed to
	 * cluster0 while cpu2 and cpu3 are fixed to cluster1 whether
	 * they are enabled or not.  Since cpu0 is the boot cpu, then
	 * cluster0 must exist.  If cpu2 or cpu3 is enabled, cluster1
	 * will exist and the cluster number is 2; otherwise the
	 * cluster number is 1.
	 */
	nclusters = 1;
	for_each_of_cpu_node(dn) {
		int cpu, err;

		err = of_property_read_u32(dn, "reg", &cpu);
		if (WARN_ON(err))
			return err;

		/* If cpu2 or cpu3 is enabled */
		if (cpu & APN806_CLUSTER_NUM_MASK) {
			nclusters = 2;
			break;
		}
	}
	/*
	 * DFS for AP806 is controlled per cluster (2 CPUs per cluster),
	 * so allocate structs per cluster
	 */
	ap_cpu_clk = devm_kcalloc(dev, nclusters, sizeof(*ap_cpu_clk),
				  GFP_KERNEL);
	if (!ap_cpu_clk)
		return -ENOMEM;

	ap_cpu_data = devm_kzalloc(dev, sizeof(*ap_cpu_data) +
				sizeof(struct clk_hw *) * nclusters,
				GFP_KERNEL);
	if (!ap_cpu_data)
		return -ENOMEM;

	for_each_of_cpu_node(dn) {
		char *clk_name = "cpu-cluster-0";
		struct clk_init_data init;
		const char *parent_name;
		struct clk *parent;
		int cpu, err;

		err = of_property_read_u32(dn, "reg", &cpu);
		if (WARN_ON(err))
			return err;

		cluster_index = cpu & APN806_CLUSTER_NUM_MASK;
		cluster_index >>= APN806_CLUSTER_NUM_OFFSET;

		/* Initialize once for one cluster */
		if (ap_cpu_data->hws[cluster_index])
			continue;

		parent = of_clk_get(np, cluster_index);
		if (IS_ERR(parent)) {
			dev_err(dev, "Could not get the clock parent\n");
			return -EINVAL;
		}
		parent_name =  __clk_get_name(parent);
		clk_name[12] += cluster_index;
		ap_cpu_clk[cluster_index].clk_name =
			ap_cp_unique_name(dev, np->parent, clk_name);
		ap_cpu_clk[cluster_index].cluster = cluster_index;
		ap_cpu_clk[cluster_index].pll_cr_base = regmap;
		ap_cpu_clk[cluster_index].hw.init = &init;
		ap_cpu_clk[cluster_index].dev = dev;

		init.name = ap_cpu_clk[cluster_index].clk_name;
		init.ops = &ap_cpu_clk_ops;
		init.num_parents = 1;
		init.parent_names = &parent_name;

		ret = devm_clk_hw_register(dev, &ap_cpu_clk[cluster_index].hw);
		if (ret)
			return ret;
		ap_cpu_data->hws[cluster_index] = &ap_cpu_clk[cluster_index].hw;
	}

	ap_cpu_data->num = cluster_index + 1;

	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, ap_cpu_data);
	if (ret)
		dev_err(dev, "failed to register OF clock provider\n");

	return ret;
}

static const struct of_device_id ap_cpu_clock_of_match[] = {
	{ .compatible = "marvell,ap806-cpu-clock", },
	{ }
};

static struct platform_driver ap_cpu_clock_driver = {
	.probe = ap_cpu_clock_probe,
	.driver		= {
		.name	= "marvell-ap-cpu-clock",
		.of_match_table = ap_cpu_clock_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(ap_cpu_clock_driver);
