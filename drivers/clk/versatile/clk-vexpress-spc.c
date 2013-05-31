/*
 * Copyright (C) 2012 ARM Limited
 * Copyright (C) 2012 Linaro
 *
 * Author: Viresh Kumar <viresh.kumar@linaro.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

/* SPC clock programming interface for Vexpress cpus */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vexpress.h>

struct clk_spc {
	struct clk_hw hw;
	spinlock_t *lock;
	int cluster;
};

#define to_clk_spc(spc) container_of(spc, struct clk_spc, hw)

static unsigned long spc_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_spc *spc = to_clk_spc(hw);
	u32 freq;

	if (vexpress_spc_get_performance(spc->cluster, &freq)) {
		return -EIO;
		pr_err("%s: Failed", __func__);
	}

	return freq * 1000;
}

static long spc_round_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long *parent_rate)
{
	return drate;
}

static int spc_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_spc *spc = to_clk_spc(hw);

	return vexpress_spc_set_performance(spc->cluster, rate / 1000);
}

static struct clk_ops clk_spc_ops = {
	.recalc_rate = spc_recalc_rate,
	.round_rate = spc_round_rate,
	.set_rate = spc_set_rate,
};

struct clk *vexpress_clk_register_spc(const char *name, int cluster_id)
{
	struct clk_init_data init;
	struct clk_spc *spc;
	struct clk *clk;

	if (!name) {
		pr_err("Invalid name passed");
		return ERR_PTR(-EINVAL);
	}

	spc = kzalloc(sizeof(*spc), GFP_KERNEL);
	if (!spc) {
		pr_err("could not allocate spc clk\n");
		return ERR_PTR(-ENOMEM);
	}

	spc->hw.init = &init;
	spc->cluster = cluster_id;

	init.name = name;
	init.ops = &clk_spc_ops;
	init.flags = CLK_IS_ROOT | CLK_GET_RATE_NOCACHE;
	init.num_parents = 0;

	clk = clk_register(NULL, &spc->hw);
	if (!IS_ERR_OR_NULL(clk))
		return clk;

	pr_err("clk register failed\n");
	kfree(spc);

	return NULL;
}

#if defined(CONFIG_OF)
void __init vexpress_clk_of_register_spc(void)
{
	char name[14] = "cpu-cluster.";
	struct device_node *node = NULL;
	struct clk *clk;
	const u32 *val;
	int cluster_id = 0, len;

	if (!of_find_compatible_node(NULL, NULL, "arm,vexpress-spc")) {
		pr_debug("%s: No SPC found, Exiting!!\n", __func__);
		return;
	}

	while ((node = of_find_node_by_name(node, "cluster"))) {
		val = of_get_property(node, "reg", &len);
		if (val && len == 4)
			cluster_id = be32_to_cpup(val);

		name[12] = cluster_id + '0';
		clk = vexpress_clk_register_spc(name, cluster_id);
		if (IS_ERR(clk))
			return;

		pr_debug("Registered clock '%s'\n", name);
		clk_register_clkdev(clk, NULL, name);
	}
}
CLK_OF_DECLARE(spc, "arm,vexpress-spc", vexpress_clk_of_register_spc);
#endif
