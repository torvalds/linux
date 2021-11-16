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
#define CPG_SD_STP_HCK		BIT(9)
#define CPG_SD_STP_CK		BIT(8)

#define CPG_SD_STP_MASK		(CPG_SD_STP_HCK | CPG_SD_STP_CK)
#define CPG_SD_FC_MASK		(0x7 << 2 | 0x3 << 0)

#define CPG_SD_DIV_TABLE_DATA(stp_hck, sd_srcfc, sd_fc, sd_div) \
{ \
	.val = ((stp_hck) ? CPG_SD_STP_HCK : 0) | \
	       ((sd_srcfc) << 2) | \
	       ((sd_fc) << 0), \
	.div = (sd_div), \
}

struct sd_div_table {
	u32 val;
	unsigned int div;
};

struct sd_clock {
	struct clk_hw hw;
	const struct sd_div_table *div_table;
	struct cpg_simple_notifier csn;
	unsigned int div_num;
	unsigned int cur_div_idx;
};

/* SDn divider
 *           sd_srcfc   sd_fc   div
 * stp_hck   (div)      (div)     = sd_srcfc x sd_fc
 *---------------------------------------------------------
 *  0         0 (1)      1 (4)      4 : SDR104 / HS200 / HS400 (8 TAP)
 *  0         1 (2)      1 (4)      8 : SDR50
 *  1         2 (4)      1 (4)     16 : HS / SDR25
 *  1         3 (8)      1 (4)     32 : NS / SDR12
 *  1         4 (16)     1 (4)     64
 *  0         0 (1)      0 (2)      2
 *  0         1 (2)      0 (2)      4 : SDR104 / HS200 / HS400 (4 TAP)
 *  1         2 (4)      0 (2)      8
 *  1         3 (8)      0 (2)     16
 *  1         4 (16)     0 (2)     32
 *
 *  NOTE: There is a quirk option to ignore the first row of the dividers
 *  table when searching for suitable settings. This is because HS400 on
 *  early ES versions of H3 and M3-W requires a specific setting to work.
 */
static const struct sd_div_table cpg_sd_div_table[] = {
/*	CPG_SD_DIV_TABLE_DATA(stp_hck,  sd_srcfc,   sd_fc,  sd_div) */
	CPG_SD_DIV_TABLE_DATA(0,        0,          1,        4),
	CPG_SD_DIV_TABLE_DATA(0,        1,          1,        8),
	CPG_SD_DIV_TABLE_DATA(1,        2,          1,       16),
	CPG_SD_DIV_TABLE_DATA(1,        3,          1,       32),
	CPG_SD_DIV_TABLE_DATA(1,        4,          1,       64),
	CPG_SD_DIV_TABLE_DATA(0,        0,          0,        2),
	CPG_SD_DIV_TABLE_DATA(0,        1,          0,        4),
	CPG_SD_DIV_TABLE_DATA(1,        2,          0,        8),
	CPG_SD_DIV_TABLE_DATA(1,        3,          0,       16),
	CPG_SD_DIV_TABLE_DATA(1,        4,          0,       32),
};

#define to_sd_clock(_hw) container_of(_hw, struct sd_clock, hw)

static int cpg_sd_clock_enable(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);

	cpg_reg_modify(clock->csn.reg, CPG_SD_STP_MASK,
		       clock->div_table[clock->cur_div_idx].val &
		       CPG_SD_STP_MASK);

	return 0;
}

static void cpg_sd_clock_disable(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);

	cpg_reg_modify(clock->csn.reg, 0, CPG_SD_STP_MASK);
}

static int cpg_sd_clock_is_enabled(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);

	return !(readl(clock->csn.reg) & CPG_SD_STP_MASK);
}

static unsigned long cpg_sd_clock_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct sd_clock *clock = to_sd_clock(hw);

	return DIV_ROUND_CLOSEST(parent_rate,
				 clock->div_table[clock->cur_div_idx].div);
}

static int cpg_sd_clock_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req)
{
	unsigned long best_rate = ULONG_MAX, diff_min = ULONG_MAX;
	struct sd_clock *clock = to_sd_clock(hw);
	unsigned long calc_rate, diff;
	unsigned int i;

	for (i = 0; i < clock->div_num; i++) {
		calc_rate = DIV_ROUND_CLOSEST(req->best_parent_rate,
					      clock->div_table[i].div);
		if (calc_rate < req->min_rate || calc_rate > req->max_rate)
			continue;

		diff = calc_rate > req->rate ? calc_rate - req->rate
					     : req->rate - calc_rate;
		if (diff < diff_min) {
			best_rate = calc_rate;
			diff_min = diff;
		}
	}

	if (best_rate == ULONG_MAX)
		return -EINVAL;

	req->rate = best_rate;
	return 0;
}

static int cpg_sd_clock_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct sd_clock *clock = to_sd_clock(hw);
	unsigned int i;

	for (i = 0; i < clock->div_num; i++)
		if (rate == DIV_ROUND_CLOSEST(parent_rate,
					      clock->div_table[i].div))
			break;

	if (i >= clock->div_num)
		return -EINVAL;

	clock->cur_div_idx = i;

	cpg_reg_modify(clock->csn.reg, CPG_SD_STP_MASK | CPG_SD_FC_MASK,
		       clock->div_table[i].val &
		       (CPG_SD_STP_MASK | CPG_SD_FC_MASK));

	return 0;
}

static const struct clk_ops cpg_sd_clock_ops = {
	.enable = cpg_sd_clock_enable,
	.disable = cpg_sd_clock_disable,
	.is_enabled = cpg_sd_clock_is_enabled,
	.recalc_rate = cpg_sd_clock_recalc_rate,
	.determine_rate = cpg_sd_clock_determine_rate,
	.set_rate = cpg_sd_clock_set_rate,
};

struct clk * __init cpg_sd_clk_register(const char *name,
	void __iomem *base, unsigned int offset, const char *parent_name,
	struct raw_notifier_head *notifiers, bool skip_first)
{
	struct clk_init_data init = {};
	struct sd_clock *clock;
	struct clk *clk;
	u32 val;

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &cpg_sd_clock_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->csn.reg = base + offset;
	clock->hw.init = &init;
	clock->div_table = cpg_sd_div_table;
	clock->div_num = ARRAY_SIZE(cpg_sd_div_table);

	if (skip_first) {
		clock->div_table++;
		clock->div_num--;
	}

	val = readl(clock->csn.reg) & ~CPG_SD_FC_MASK;
	val |= CPG_SD_STP_MASK | (clock->div_table[0].val & CPG_SD_FC_MASK);
	writel(val, clock->csn.reg);

	clk = clk_register(NULL, &clock->hw);
	if (IS_ERR(clk))
		goto free_clock;

	cpg_simple_notifier_register(notifiers, &clock->csn);
	return clk;

free_clock:
	kfree(clock);
	return clk;
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

