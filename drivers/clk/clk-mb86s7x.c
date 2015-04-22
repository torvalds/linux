/*
 * Copyright (C) 2013-2015 FUJITSU SEMICONDUCTOR LIMITED
 * Copyright (C) 2015 Linaro Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/cpu.h>
#include <linux/clk-provider.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/topology.h>
#include <linux/mailbox_client.h>
#include <linux/platform_device.h>

#include <soc/mb86s7x/scb_mhu.h>

#define to_crg_clk(p) container_of(p, struct crg_clk, hw)
#define to_clc_clk(p) container_of(p, struct cl_clk, hw)

struct mb86s7x_peri_clk {
	u32 payload_size;
	u32 cntrlr;
	u32 domain;
	u32 port;
	u32 en;
	u64 frequency;
} __packed __aligned(4);

struct hack_rate {
	unsigned clk_id;
	unsigned long rate;
	int gated;
};

struct crg_clk {
	struct clk_hw hw;
	u8 cntrlr, domain, port;
};

static int crg_gate_control(struct clk_hw *hw, int en)
{
	struct crg_clk *crgclk = to_crg_clk(hw);
	struct mb86s7x_peri_clk cmd;
	int ret;

	cmd.payload_size = sizeof(cmd);
	cmd.cntrlr = crgclk->cntrlr;
	cmd.domain = crgclk->domain;
	cmd.port = crgclk->port;
	cmd.en = en;

	/* Port is UngatedCLK */
	if (cmd.port == 8)
		return en ? 0 : -EINVAL;

	pr_debug("%s:%d CMD Cntrlr-%u Dom-%u Port-%u En-%u}\n",
		 __func__, __LINE__, cmd.cntrlr,
		 cmd.domain, cmd.port, cmd.en);

	ret = mb86s7x_send_packet(CMD_PERI_CLOCK_GATE_SET_REQ,
				  &cmd, sizeof(cmd));
	if (ret < 0) {
		pr_err("%s:%d failed!\n", __func__, __LINE__);
		return ret;
	}

	pr_debug("%s:%d REP Cntrlr-%u Dom-%u Port-%u En-%u}\n",
		 __func__, __LINE__, cmd.cntrlr,
		 cmd.domain, cmd.port, cmd.en);

	/* If the request was rejected */
	if (cmd.en != en)
		ret = -EINVAL;
	else
		ret = 0;

	return ret;
}

static int crg_port_prepare(struct clk_hw *hw)
{
	return crg_gate_control(hw, 1);
}

static void crg_port_unprepare(struct clk_hw *hw)
{
	crg_gate_control(hw, 0);
}

static int
crg_rate_control(struct clk_hw *hw, int set, unsigned long *rate)
{
	struct crg_clk *crgclk = to_crg_clk(hw);
	struct mb86s7x_peri_clk cmd;
	int code, ret;

	cmd.payload_size = sizeof(cmd);
	cmd.cntrlr = crgclk->cntrlr;
	cmd.domain = crgclk->domain;
	cmd.port = crgclk->port;
	cmd.frequency = *rate;

	if (set) {
		code = CMD_PERI_CLOCK_RATE_SET_REQ;
		pr_debug("%s:%d CMD Cntrlr-%u Dom-%u Port-%u Rate-SET %lluHz}\n",
			 __func__, __LINE__, cmd.cntrlr,
			 cmd.domain, cmd.port, cmd.frequency);
	} else {
		code = CMD_PERI_CLOCK_RATE_GET_REQ;
		pr_debug("%s:%d CMD Cntrlr-%u Dom-%u Port-%u Rate-GET}\n",
			 __func__, __LINE__, cmd.cntrlr,
			 cmd.domain, cmd.port);
	}

	ret = mb86s7x_send_packet(code, &cmd, sizeof(cmd));
	if (ret < 0) {
		pr_err("%s:%d failed!\n", __func__, __LINE__);
		return ret;
	}

	if (set)
		pr_debug("%s:%d REP Cntrlr-%u Dom-%u Port-%u Rate-SET %lluHz}\n",
			 __func__, __LINE__, cmd.cntrlr,
			 cmd.domain, cmd.port, cmd.frequency);
	else
		pr_debug("%s:%d REP Cntrlr-%u Dom-%u Port-%u Rate-GOT %lluHz}\n",
			 __func__, __LINE__, cmd.cntrlr,
			 cmd.domain, cmd.port, cmd.frequency);

	*rate = cmd.frequency;
	return 0;
}

static unsigned long
crg_port_recalc_rate(struct clk_hw *hw,	unsigned long parent_rate)
{
	unsigned long rate;

	crg_rate_control(hw, 0, &rate);

	return rate;
}

static long
crg_port_round_rate(struct clk_hw *hw,
		    unsigned long rate, unsigned long *pr)
{
	return rate;
}

static int
crg_port_set_rate(struct clk_hw *hw,
		  unsigned long rate, unsigned long parent_rate)
{
	return crg_rate_control(hw, 1, &rate);
}

const struct clk_ops crg_port_ops = {
	.prepare = crg_port_prepare,
	.unprepare = crg_port_unprepare,
	.recalc_rate = crg_port_recalc_rate,
	.round_rate = crg_port_round_rate,
	.set_rate = crg_port_set_rate,
};

struct mb86s70_crg11 {
	struct mutex lock; /* protects CLK populating and searching */
};

static struct clk *crg11_get(struct of_phandle_args *clkspec, void *data)
{
	struct mb86s70_crg11 *crg11 = data;
	struct clk_init_data init;
	u32 cntrlr, domain, port;
	struct crg_clk *crgclk;
	struct clk *clk;
	char clkp[20];

	if (clkspec->args_count != 3)
		return ERR_PTR(-EINVAL);

	cntrlr = clkspec->args[0];
	domain = clkspec->args[1];
	port = clkspec->args[2];

	if (port > 7)
		snprintf(clkp, 20, "UngatedCLK%d_%X", cntrlr, domain);
	else
		snprintf(clkp, 20, "CLK%d_%X_%d", cntrlr, domain, port);

	mutex_lock(&crg11->lock);

	clk = __clk_lookup(clkp);
	if (clk) {
		mutex_unlock(&crg11->lock);
		return clk;
	}

	crgclk = kzalloc(sizeof(*crgclk), GFP_KERNEL);
	if (!crgclk) {
		mutex_unlock(&crg11->lock);
		return ERR_PTR(-ENOMEM);
	}

	init.name = clkp;
	init.num_parents = 0;
	init.ops = &crg_port_ops;
	init.flags = CLK_IS_ROOT;
	crgclk->hw.init = &init;
	crgclk->cntrlr = cntrlr;
	crgclk->domain = domain;
	crgclk->port = port;
	clk = clk_register(NULL, &crgclk->hw);
	if (IS_ERR(clk))
		pr_err("%s:%d Error!\n", __func__, __LINE__);
	else
		pr_debug("Registered %s\n", clkp);

	clk_register_clkdev(clk, clkp, NULL);
	mutex_unlock(&crg11->lock);
	return clk;
}

static void __init crg_port_init(struct device_node *node)
{
	struct mb86s70_crg11 *crg11;

	crg11 = kzalloc(sizeof(*crg11), GFP_KERNEL);
	if (!crg11)
		return;

	mutex_init(&crg11->lock);

	of_clk_add_provider(node, crg11_get, crg11);
}
CLK_OF_DECLARE(crg11_gate, "fujitsu,mb86s70-crg11", crg_port_init);

struct cl_clk {
	struct clk_hw hw;
	int cluster;
};

struct mb86s7x_cpu_freq {
	u32 payload_size;
	u32 cluster_class;
	u32 cluster_id;
	u32 cpu_id;
	u64 frequency;
};

static void mhu_cluster_rate(struct clk_hw *hw, unsigned long *rate, int get)
{
	struct cl_clk *clc = to_clc_clk(hw);
	struct mb86s7x_cpu_freq cmd;
	int code, ret;

	cmd.payload_size = sizeof(cmd);
	cmd.cluster_class = 0;
	cmd.cluster_id = clc->cluster;
	cmd.cpu_id = 0;
	cmd.frequency = *rate;

	if (get)
		code = CMD_CPU_CLOCK_RATE_GET_REQ;
	else
		code = CMD_CPU_CLOCK_RATE_SET_REQ;

	pr_debug("%s:%d CMD Cl_Class-%u CL_ID-%u CPU_ID-%u Freq-%llu}\n",
		 __func__, __LINE__, cmd.cluster_class,
		 cmd.cluster_id, cmd.cpu_id, cmd.frequency);

	ret = mb86s7x_send_packet(code, &cmd, sizeof(cmd));
	if (ret < 0) {
		pr_err("%s:%d failed!\n", __func__, __LINE__);
		return;
	}

	pr_debug("%s:%d REP Cl_Class-%u CL_ID-%u CPU_ID-%u Freq-%llu}\n",
		 __func__, __LINE__, cmd.cluster_class,
		 cmd.cluster_id, cmd.cpu_id, cmd.frequency);

	*rate = cmd.frequency;
}

static unsigned long
clc_recalc_rate(struct clk_hw *hw, unsigned long unused)
{
	unsigned long rate;

	mhu_cluster_rate(hw, &rate, 1);
	return rate;
}

static long
clc_round_rate(struct clk_hw *hw, unsigned long rate,
	       unsigned long *unused)
{
	return rate;
}

static int
clc_set_rate(struct clk_hw *hw, unsigned long rate,
	     unsigned long unused)
{
	unsigned long res = rate;

	mhu_cluster_rate(hw, &res, 0);

	return (res == rate) ? 0 : -EINVAL;
}

static struct clk_ops clk_clc_ops = {
	.recalc_rate = clc_recalc_rate,
	.round_rate = clc_round_rate,
	.set_rate = clc_set_rate,
};

struct clk *mb86s7x_clclk_register(struct device *cpu_dev)
{
	struct clk_init_data init;
	struct cl_clk *clc;

	clc = kzalloc(sizeof(*clc), GFP_KERNEL);
	if (!clc)
		return ERR_PTR(-ENOMEM);

	clc->hw.init = &init;
	clc->cluster = topology_physical_package_id(cpu_dev->id);

	init.name = dev_name(cpu_dev);
	init.ops = &clk_clc_ops;
	init.flags = CLK_IS_ROOT | CLK_GET_RATE_NOCACHE;
	init.num_parents = 0;

	return devm_clk_register(cpu_dev, &clc->hw);
}

static int mb86s7x_clclk_of_init(void)
{
	int cpu, ret = -ENODEV;
	struct device_node *np;
	struct clk *clk;

	np = of_find_compatible_node(NULL, NULL, "fujitsu,mb86s70-scb-1.0");
	if (!np || !of_device_is_available(np))
		goto exit;

	for_each_possible_cpu(cpu) {
		struct device *cpu_dev = get_cpu_device(cpu);

		if (!cpu_dev) {
			pr_err("failed to get cpu%d device\n", cpu);
			continue;
		}

		clk = mb86s7x_clclk_register(cpu_dev);
		if (IS_ERR(clk)) {
			pr_err("failed to register cpu%d clock\n", cpu);
			continue;
		}
		if (clk_register_clkdev(clk, NULL, dev_name(cpu_dev))) {
			pr_err("failed to register cpu%d clock lookup\n", cpu);
			continue;
		}
		pr_debug("registered clk for %s\n", dev_name(cpu_dev));
	}
	ret = 0;

	platform_device_register_simple("arm-bL-cpufreq-dt", -1, NULL, 0);
exit:
	of_node_put(np);
	return ret;
}
module_init(mb86s7x_clclk_of_init);
