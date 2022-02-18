// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car Gen3 Clock Pulse Generator Library
 *
 * Copyright (C) 2015-2018 Glider bvba
 * Copyright (C) 2019 Renesas Electronics Corp.
 *
 * Based on clk-rcar-gen3.c
 *
 * Copyright (C) 2015 Renesas Electronics Corp.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include "rcar-cpg-lib.h"

spinlock_t cpg_lock;

void cpg_reg_modify(void __iomem *reg, u32 clear, u32 set)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&cpg_lock, flags);
	val = readl(reg);
	val &= ~clear;
	val |= set;
	writel(val, reg);
	spin_unlock_irqrestore(&cpg_lock, flags);
};

static int cpg_simple_notifier_call(struct notifier_block *nb,
				    unsigned long action, void *data)
{
	struct cpg_simple_notifier *csn =
		container_of(nb, struct cpg_simple_notifier, nb);

	switch (action) {
	case PM_EVENT_SUSPEND:
		csn->saved = readl(csn->reg);
		return NOTIFY_OK;

	case PM_EVENT_RESUME:
		writel(csn->saved, csn->reg);
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

void cpg_simple_notifier_register(struct raw_notifier_head *notifiers,
				  struct cpg_simple_notifier *csn)
{
	csn->nb.notifier_call = cpg_simple_notifier_call;
	raw_notifier_chain_register(notifiers, &csn->nb);
}

/*
 * SDn Clock
 */

#define SDnSRCFC_SHIFT 2
#define STPnHCK	BIT(9 - SDnSRCFC_SHIFT)

static const struct clk_div_table cpg_sdh_div_table[] = {
	{ 0, 1 }, { 1, 2 }, { STPnHCK | 2, 4 }, { STPnHCK | 3, 8 },
	{ STPnHCK | 4, 16 }, { 0, 0 },
};

struct clk * __init cpg_sdh_clk_register(const char *name,
	void __iomem *sdnckcr, const char *parent_name,
	struct raw_notifier_head *notifiers)
{
	struct cpg_simple_notifier *csn;
	struct clk *clk;

	csn = kzalloc(sizeof(*csn), GFP_KERNEL);
	if (!csn)
		return ERR_PTR(-ENOMEM);

	csn->reg = sdnckcr;

	clk = clk_register_divider_table(NULL, name, parent_name, 0, sdnckcr,
					 SDnSRCFC_SHIFT, 8, 0, cpg_sdh_div_table,
					 &cpg_lock);
	if (IS_ERR(clk)) {
		kfree(csn);
		return clk;
	}

	cpg_simple_notifier_register(notifiers, csn);
	return clk;
}

static const struct clk_div_table cpg_sd_div_table[] = {
	{ 0, 2 }, { 1, 4 }, { 0, 0 },
};

struct clk * __init cpg_sd_clk_register(const char *name,
	void __iomem *sdnckcr, const char *parent_name)
{
	return clk_register_divider_table(NULL, name, parent_name, 0, sdnckcr,
					  0, 2, 0, cpg_sd_div_table, &cpg_lock);
}

struct rpc_clock {
	struct clk_divider div;
	struct clk_gate gate;
	/*
	 * One notifier covers both RPC and RPCD2 clocks as they are both
	 * controlled by the same RPCCKCR register...
	 */
	struct cpg_simple_notifier csn;
};

static const struct clk_div_table cpg_rpc_div_table[] = {
	{ 1, 2 }, { 3, 4 }, { 5, 6 }, { 7, 8 }, { 0, 0 },
};

struct clk * __init cpg_rpc_clk_register(const char *name,
	void __iomem *rpcckcr, const char *parent_name,
	struct raw_notifier_head *notifiers)
{
	struct rpc_clock *rpc;
	struct clk *clk;

	rpc = kzalloc(sizeof(*rpc), GFP_KERNEL);
	if (!rpc)
		return ERR_PTR(-ENOMEM);

	rpc->div.reg = rpcckcr;
	rpc->div.width = 3;
	rpc->div.table = cpg_rpc_div_table;
	rpc->div.lock = &cpg_lock;

	rpc->gate.reg = rpcckcr;
	rpc->gate.bit_idx = 8;
	rpc->gate.flags = CLK_GATE_SET_TO_DISABLE;
	rpc->gate.lock = &cpg_lock;

	rpc->csn.reg = rpcckcr;

	clk = clk_register_composite(NULL, name, &parent_name, 1, NULL, NULL,
				     &rpc->div.hw,  &clk_divider_ops,
				     &rpc->gate.hw, &clk_gate_ops,
				     CLK_SET_RATE_PARENT);
	if (IS_ERR(clk)) {
		kfree(rpc);
		return clk;
	}

	cpg_simple_notifier_register(notifiers, &rpc->csn);
	return clk;
}

struct rpcd2_clock {
	struct clk_fixed_factor fixed;
	struct clk_gate gate;
};

struct clk * __init cpg_rpcd2_clk_register(const char *name,
					   void __iomem *rpcckcr,
					   const char *parent_name)
{
	struct rpcd2_clock *rpcd2;
	struct clk *clk;

	rpcd2 = kzalloc(sizeof(*rpcd2), GFP_KERNEL);
	if (!rpcd2)
		return ERR_PTR(-ENOMEM);

	rpcd2->fixed.mult = 1;
	rpcd2->fixed.div = 2;

	rpcd2->gate.reg = rpcckcr;
	rpcd2->gate.bit_idx = 9;
	rpcd2->gate.flags = CLK_GATE_SET_TO_DISABLE;
	rpcd2->gate.lock = &cpg_lock;

	clk = clk_register_composite(NULL, name, &parent_name, 1, NULL, NULL,
				     &rpcd2->fixed.hw, &clk_fixed_factor_ops,
				     &rpcd2->gate.hw, &clk_gate_ops,
				     CLK_SET_RATE_PARENT);
	if (IS_ERR(clk))
		kfree(rpcd2);

	return clk;
}

