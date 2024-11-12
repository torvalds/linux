// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013, 2016, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/clk/qcom.h>

#include "clk-branch.h"
#include "clk-debug.h"
#include "clk-regmap.h"

static bool clk_branch_in_hwcg_mode(const struct clk_branch *br)
{
	u32 val;

	if (!br->hwcg_reg)
		return false;

	regmap_read(br->clkr.regmap, br->hwcg_reg, &val);

	return !!(val & BIT(br->hwcg_bit));
}

static bool clk_branch_check_halt(const struct clk_branch *br, bool enabling)
{
	bool invert = (br->halt_check == BRANCH_HALT_ENABLE);
	u32 val;

	regmap_read(br->clkr.regmap, br->halt_reg, &val);

	val &= BIT(br->halt_bit);
	if (invert)
		val = !val;

	return !!val == !enabling;
}

#define BRANCH_CLK_OFF			BIT(31)
#define BRANCH_NOC_FSM_STATUS_SHIFT	28
#define BRANCH_NOC_FSM_STATUS_MASK	0x7
#define BRANCH_NOC_FSM_STATUS_ON	(0x2 << BRANCH_NOC_FSM_STATUS_SHIFT)
#define BRANCH_CLK_DIS_MASK		BIT(22)

static bool clk_branch2_check_halt(const struct clk_branch *br, bool enabling)
{
	u32 val;
	u32 mask;

	mask = BRANCH_NOC_FSM_STATUS_MASK << BRANCH_NOC_FSM_STATUS_SHIFT;
	mask |= BRANCH_CLK_OFF;

	regmap_read(br->clkr.regmap, br->halt_reg, &val);

	if (enabling) {
		val &= mask;

		if (br->halt_check == BRANCH_HALT_INVERT)
			return (val & BRANCH_CLK_OFF) == BRANCH_CLK_OFF;

		return (val & BRANCH_CLK_OFF) == 0 ||
			val == BRANCH_NOC_FSM_STATUS_ON;
	} else {
		return val & BRANCH_CLK_OFF;
	}
}

static int get_branch_timeout(const struct clk_branch *br)
{
	int rate, period_us, timeout;

	/*
	 * The time it takes a clock branch to toggle is roughly 3 clock cycles.
	 */
	rate = clk_hw_get_rate(&br->clkr.hw);
	period_us = 1000000 / rate;
	timeout = 3 * period_us;

	return max(timeout, 200);
}

static int clk_branch_wait(const struct clk_branch *br, bool enabling,
		bool (check_halt)(const struct clk_branch *, bool))
{
	int timeout, count;

	bool voted = br->halt_check & BRANCH_VOTED;
	/*
	 * Skip checking halt bit if we're explicitly ignoring the bit or the
	 * clock is in hardware gated mode
	 */
	if (br->halt_check == BRANCH_HALT_SKIP || clk_branch_in_hwcg_mode(br))
		return 0;

	if (br->halt_check == BRANCH_HALT_DELAY || (!enabling && voted)) {
		udelay(10);
	} else if (br->halt_check == BRANCH_HALT_ENABLE ||
		   br->halt_check == BRANCH_HALT ||
		   br->halt_check == BRANCH_HALT_POLL ||
		   (enabling && voted)) {
		timeout = get_branch_timeout(br);

		for (count = timeout; count > 0; count--) {
			if (check_halt(br, enabling))
				return 0;
			udelay(1);
		}
		WARN_CLK((struct clk_hw *)&br->clkr.hw, 1, "status stuck at 'o%s' after %d us",
			 enabling ? "ff" : "n", timeout);
		return -EBUSY;
	}
	return 0;
}

static int clk_branch_toggle(struct clk_hw *hw, bool en,
		bool (check_halt)(const struct clk_branch *, bool))
{
	struct clk_branch *br = to_clk_branch(hw);
	int ret;

	if (br->halt_check == BRANCH_HALT_POLL) {
		return  clk_branch_wait(br, en, check_halt);
	}

	if (en) {
		ret = clk_enable_regmap(hw);
		if (ret)
			return ret;
	} else {
		clk_disable_regmap(hw);
	}

	return clk_branch_wait(br, en, check_halt);
}

static int clk_branch_enable(struct clk_hw *hw)
{
	return clk_branch_toggle(hw, true, clk_branch_check_halt);
}

static void clk_branch_disable(struct clk_hw *hw)
{
	clk_branch_toggle(hw, false, clk_branch_check_halt);
}

static void clk_branch_debug_init(struct clk_hw *hw, struct dentry *dentry)
{
	clk_common_debug_init(hw, dentry);
	clk_debug_measure_add(hw, dentry);
}

const struct clk_ops clk_branch_ops = {
	.enable = clk_branch_enable,
	.disable = clk_branch_disable,
	.is_enabled = clk_is_enabled_regmap,
	.debug_init = clk_branch_debug_init,
};
EXPORT_SYMBOL_GPL(clk_branch_ops);

static int clk_branch2_enable(struct clk_hw *hw)
{
	return clk_branch_toggle(hw, true, clk_branch2_check_halt);
}

static void clk_branch2_disable(struct clk_hw *hw)
{
	clk_branch_toggle(hw, false, clk_branch2_check_halt);
}

static int clk_branch2_mem_enable(struct clk_hw *hw)
{
	struct clk_mem_branch *mem_br = to_clk_mem_branch(hw);
	struct clk_branch branch = mem_br->branch;
	u32 val;
	int ret;

	if (mem_br->mem_enable_invert)
		regmap_update_bits(branch.clkr.regmap, mem_br->mem_enable_reg,
			   mem_br->mem_enable_mask, 0);
	else
		regmap_update_bits(branch.clkr.regmap, mem_br->mem_enable_reg,
			   mem_br->mem_enable_ack_mask, mem_br->mem_enable_ack_mask);

	ret = regmap_read_poll_timeout(branch.clkr.regmap, mem_br->mem_ack_reg,
				       val, val & mem_br->mem_enable_ack_mask, 0, 200);
	if (ret) {
		WARN(1, "%s mem enable failed\n", clk_hw_get_name(&branch.clkr.hw));
		return ret;
	}

	return clk_branch2_enable(hw);
}

static void clk_branch2_mem_disable(struct clk_hw *hw)
{
	struct clk_mem_branch *mem_br = to_clk_mem_branch(hw);

	if (mem_br->mem_enable_invert)
		regmap_update_bits(mem_br->branch.clkr.regmap, mem_br->mem_enable_reg,
				mem_br->mem_enable_mask, mem_br->mem_enable_mask);
	else
		regmap_update_bits(mem_br->branch.clkr.regmap, mem_br->mem_enable_reg,
			   mem_br->mem_enable_ack_mask, 0);

	return clk_branch2_disable(hw);
}

static int clk_branch2_force_off_enable(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	regmap_update_bits(rclk->regmap, rclk->enable_reg,
			   BRANCH_CLK_DIS_MASK,
			   0x0);
	return clk_branch2_enable(hw);
}

static void clk_branch2_force_off_disable(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	regmap_update_bits(rclk->regmap, rclk->enable_reg,
			   BRANCH_CLK_DIS_MASK,
			   BRANCH_CLK_DIS_MASK);
	clk_branch2_disable(hw);
}

static void clk_branch2_list_registers(struct seq_file *f, struct clk_hw *hw)
{
	struct clk_branch *br = to_clk_branch(hw);
	struct clk_regmap *rclk = to_clk_regmap(hw);
	int size, i, val;

	static struct clk_register_data data[] = {
		{"CBCR", 0x0},
	};

	static struct clk_register_data data1[] = {
		{"APSS_VOTE", 0x0},
		{"APSS_SLEEP_VOTE", 0x4},
	};

	static struct clk_register_data data3[] = {
		{"SREG_ENABLE_REG", 0x0},
		{"SREG_CORE_ACK_MASK", 0x0},
		{"SREG_PERIPH_ACK_MASK", 0x0},
	};

	size = ARRAY_SIZE(data);

	for (i = 0; i < size; i++) {
		regmap_read(br->clkr.regmap, br->halt_reg + data[i].offset,
					&val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[i].name, val);
	}

	if ((br->halt_check & BRANCH_HALT_VOTED) &&
			!(br->halt_check & BRANCH_VOTED)) {
		if (rclk->enable_reg) {
			size = ARRAY_SIZE(data1);
			for (i = 0; i < size; i++) {
				regmap_read(br->clkr.regmap, rclk->enable_reg +
						data1[i].offset, &val);
				clock_debug_output(f, "%20s: 0x%.8x\n",
						data1[i].name, val);
			}
		}
	}

	if (br->sreg_enable_reg) {
		regmap_read(br->clkr.regmap, br->sreg_enable_reg +
						data3[0].offset, &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data3[0].name, val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data3[1].name,
						br->sreg_core_ack_bit);
		clock_debug_output(f, "%20s: 0x%.8x\n", data3[2].name,
						br->sreg_periph_ack_bit);
	}
}

static void clk_branch2_mem_list_registers(struct seq_file *f, struct clk_hw *hw)
{
	struct clk_mem_branch *mem_br = to_clk_mem_branch(hw);
	struct clk_branch *br = &mem_br->branch;
	u32 val;

	static struct clk_register_data data[] = {
		{"CBCR", 0x0},
		{"MEM_ENABLE", 0x0},
		{"MEM_ENABLE_ACK", 0x0},
		{"MEM_ENABLE_ACK_MASK", 0x0},
	};

	regmap_read(br->clkr.regmap, br->halt_reg + data[0].offset,
				&val);
	clock_debug_output(f, "%20s: 0x%.8x\n", data[0].name, val);

	if (mem_br->mem_enable_reg && mem_br->mem_ack_reg) {
		regmap_read(mem_br->branch.clkr.regmap, mem_br->mem_enable_reg +
						data[1].offset, &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[1].name, val);

		regmap_read(mem_br->branch.clkr.regmap, mem_br->mem_ack_reg +
						data[2].offset, &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[2].name, val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[3].name,
						mem_br->mem_enable_ack_mask);
	}
}

static int clk_branch2_set_flags(struct clk_hw *hw, unsigned long flags)
{
	struct clk_branch *br = to_clk_branch(hw);
	u32 cbcr_val = 0, cbcr_mask;
	int ret;

	switch (flags) {
	case CLKFLAG_PERIPH_OFF_SET:
		cbcr_val = cbcr_mask = BIT(12);
		break;
	case CLKFLAG_PERIPH_OFF_CLEAR:
		cbcr_mask = BIT(12);
		break;
	case CLKFLAG_RETAIN_PERIPH:
		cbcr_val = cbcr_mask = BIT(13);
		break;
	case CLKFLAG_NORETAIN_PERIPH:
		cbcr_mask = BIT(13);
		break;
	case CLKFLAG_RETAIN_MEM:
		cbcr_val = cbcr_mask = BIT(14);
		break;
	case CLKFLAG_NORETAIN_MEM:
		cbcr_mask = BIT(14);
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(br->clkr.regmap, br->halt_reg, cbcr_mask,
								cbcr_val);
	/* Make sure power is enabled/disabled before returning. */
	mb();

	udelay(1);

	return ret;
}

static struct clk_regmap_ops clk_branch2_regmap_ops = {
	.list_registers = clk_branch2_list_registers,
	.set_flags = clk_branch2_set_flags,
};

static struct clk_regmap_ops clk_branch2_mem_regmap_ops = {
	.list_registers = clk_branch2_mem_list_registers,
	.set_flags = clk_branch2_set_flags,
};

static int clk_branch2_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_branch2_regmap_ops;

	return 0;
}

static int clk_branch2_mem_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_branch2_mem_regmap_ops;

	return 0;
}

static int clk_branch2_sreg_enable(struct clk_hw *hw)
{
	struct clk_branch *br = to_clk_branch(hw);
	u32 val;
	int count = 200;
	int ret;

	ret = clk_enable_regmap(hw);
	if (ret)
		return -EINVAL;

	regmap_read(br->clkr.regmap, br->sreg_enable_reg, &val);

	while (count-- > 0) {
		if (!(val & br->sreg_core_ack_bit))
			return 0;
		udelay(1);
		regmap_read(br->clkr.regmap, br->sreg_enable_reg, &val);
	}

	return -EBUSY;
}

static void clk_branch2_sreg_disable(struct clk_hw *hw)
{
	struct clk_branch *br = to_clk_branch(hw);
	u32 val;
	int count = 200;

	clk_disable_regmap(hw);

	regmap_read(br->clkr.regmap, br->sreg_enable_reg, &val);

	while (count-- > 0) {
		if (val & br->sreg_periph_ack_bit)
			return;
		udelay(1);
		regmap_read(br->clkr.regmap, br->sreg_enable_reg, &val);
	}
}

static void clk_branch_restore_context_aon(struct clk_hw *hw)
{
	if (clk_enable_regmap(hw))
		pr_err("Failed to enable %s\n", clk_hw_get_name(hw));
}

static void clk_branch_restore_context(struct clk_hw *hw)
{
	if (!(clk_hw_get_flags(hw) & CLK_IS_CRITICAL))
		return;

	if (clk_enable_regmap(hw))
		pr_err("Failed to enable %s\n", clk_hw_get_name(hw));
}

const struct clk_ops clk_branch2_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_branch2_enable,
	.disable = clk_branch2_disable,
	.is_enabled = clk_is_enabled_regmap,
	.init = clk_branch2_init,
	.debug_init = clk_branch_debug_init,
	.restore_context = clk_branch_restore_context,
};
EXPORT_SYMBOL_GPL(clk_branch2_ops);

const struct clk_ops clk_branch2_crm_ops = {
	.is_enabled = clk_is_enabled_regmap,
	.init = clk_branch2_init,
	.debug_init = clk_branch_debug_init,
};
EXPORT_SYMBOL(clk_branch2_crm_ops);

const struct clk_ops clk_branch2_aon_ops = {
	.enable = clk_branch2_enable,
	.restore_context = clk_branch_restore_context_aon,
	.is_enabled = clk_is_enabled_regmap,
	.init = clk_branch2_init,
	.debug_init = clk_branch_debug_init,
};
EXPORT_SYMBOL_GPL(clk_branch2_aon_ops);

const struct clk_ops clk_branch2_force_off_ops = {
	.enable = clk_branch2_force_off_enable,
	.disable = clk_branch2_force_off_disable,
	.is_enabled = clk_is_enabled_regmap,
	.init = clk_branch2_init,
	.debug_init = clk_branch_debug_init,
};
EXPORT_SYMBOL(clk_branch2_force_off_ops);

const struct clk_ops clk_branch2_mem_ops = {
	.enable = clk_branch2_mem_enable,
	.disable = clk_branch2_mem_disable,
	.is_enabled = clk_is_enabled_regmap,
	.init = clk_branch2_mem_init,
	.debug_init = clk_branch_debug_init,
};
EXPORT_SYMBOL_GPL(clk_branch2_mem_ops);

const struct clk_ops clk_branch2_sreg_ops = {
	.enable = clk_branch2_sreg_enable,
	.disable = clk_branch2_sreg_disable,
	.is_enabled = clk_is_enabled_regmap,
	.init = clk_branch2_init,
	.debug_init = clk_branch_debug_init,
};
EXPORT_SYMBOL_GPL(clk_branch2_sreg_ops);

static unsigned long clk_branch2_hw_ctl_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return parent_rate;
}

static int clk_branch2_hw_ctl_determine_rate(struct clk_hw *hw,
		struct clk_rate_request *req)
{
	struct clk_hw *clkp;

	clkp = clk_hw_get_parent(hw);
	if (!clkp)
		return -EINVAL;

	req->best_parent_hw = clkp;
	req->best_parent_rate = clk_round_rate(clkp->clk, req->rate);

	return 0;
}

static int clk_branch2_hw_ctl_enable(struct clk_hw *hw)
{
	struct clk_hw *parent = clk_hw_get_parent(hw);

	/* The parent branch clock should have been prepared prior to this. */
	if (!parent || (parent && !clk_hw_is_prepared(parent)))
		return -EINVAL;

	return clk_enable_regmap(hw);
}

static void clk_branch2_hw_ctl_disable(struct clk_hw *hw)
{
	struct clk_hw *parent = clk_hw_get_parent(hw);

	if (!parent)
		return;

	clk_disable_regmap(hw);
}

const struct clk_ops clk_branch2_hw_ctl_ops = {
	.enable = clk_branch2_hw_ctl_enable,
	.disable = clk_branch2_hw_ctl_disable,
	.is_enabled = clk_is_enabled_regmap,
	.recalc_rate = clk_branch2_hw_ctl_recalc_rate,
	.determine_rate = clk_branch2_hw_ctl_determine_rate,
	.init = clk_branch2_init,
	.debug_init = clk_branch_debug_init,
};
EXPORT_SYMBOL(clk_branch2_hw_ctl_ops);

const struct clk_ops clk_branch_simple_ops = {
	.enable = clk_enable_regmap,
	.disable = clk_disable_regmap,
	.is_enabled = clk_is_enabled_regmap,
	.init = clk_branch2_init,
	.debug_init = clk_branch_debug_init,
};
EXPORT_SYMBOL_GPL(clk_branch_simple_ops);
