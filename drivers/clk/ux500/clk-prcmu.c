/*
 * PRCMU clock implementation for ux500 platform.
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/clk-provider.h>
#include <linux/clk-private.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include "clk.h"

#define to_clk_prcmu(_hw) container_of(_hw, struct clk_prcmu, hw)

struct clk_prcmu {
	struct clk_hw hw;
	u8 cg_sel;
	int is_enabled;
};

/* PRCMU clock operations. */

static int clk_prcmu_prepare(struct clk_hw *hw)
{
	struct clk_prcmu *clk = to_clk_prcmu(hw);
	return prcmu_request_clock(clk->cg_sel, true);
}

static void clk_prcmu_unprepare(struct clk_hw *hw)
{
	struct clk_prcmu *clk = to_clk_prcmu(hw);
	if (prcmu_request_clock(clk->cg_sel, false))
		pr_err("clk_prcmu: %s failed to disable %s.\n", __func__,
			hw->init->name);
}

static int clk_prcmu_enable(struct clk_hw *hw)
{
	struct clk_prcmu *clk = to_clk_prcmu(hw);
	clk->is_enabled = 1;
	return 0;
}

static void clk_prcmu_disable(struct clk_hw *hw)
{
	struct clk_prcmu *clk = to_clk_prcmu(hw);
	clk->is_enabled = 0;
}

static int clk_prcmu_is_enabled(struct clk_hw *hw)
{
	struct clk_prcmu *clk = to_clk_prcmu(hw);
	return clk->is_enabled;
}

static unsigned long clk_prcmu_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct clk_prcmu *clk = to_clk_prcmu(hw);
	return prcmu_clock_rate(clk->cg_sel);
}

static long clk_prcmu_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *parent_rate)
{
	struct clk_prcmu *clk = to_clk_prcmu(hw);
	return prcmu_round_clock_rate(clk->cg_sel, rate);
}

static int clk_prcmu_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct clk_prcmu *clk = to_clk_prcmu(hw);
	return prcmu_set_clock_rate(clk->cg_sel, rate);
}

static int request_ape_opp100(bool enable)
{
	static int reqs;
	int err = 0;

	if (enable) {
		if (!reqs)
			err = prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP,
							"clock", 100);
		if (!err)
			reqs++;
	} else {
		reqs--;
		if (!reqs)
			prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP,
						"clock");
	}
	return err;
}

static int clk_prcmu_opp_prepare(struct clk_hw *hw)
{
	int err;
	struct clk_prcmu *clk = to_clk_prcmu(hw);

	err = request_ape_opp100(true);
	if (err) {
		pr_err("clk_prcmu: %s failed to request APE OPP100 for %s.\n",
			__func__, hw->init->name);
		return err;
	}

	err = prcmu_request_clock(clk->cg_sel, true);
	if (err)
		request_ape_opp100(false);

	return err;
}

static void clk_prcmu_opp_unprepare(struct clk_hw *hw)
{
	struct clk_prcmu *clk = to_clk_prcmu(hw);

	if (prcmu_request_clock(clk->cg_sel, false))
		goto out_error;
	if (request_ape_opp100(false))
		goto out_error;
	return;

out_error:
	pr_err("clk_prcmu: %s failed to disable %s.\n", __func__,
		hw->init->name);
}

static int clk_prcmu_opp_volt_prepare(struct clk_hw *hw)
{
	int err;
	struct clk_prcmu *clk = to_clk_prcmu(hw);

	err = prcmu_request_ape_opp_100_voltage(true);
	if (err) {
		pr_err("clk_prcmu: %s failed to request APE OPP VOLT for %s.\n",
			__func__, hw->init->name);
		return err;
	}

	err = prcmu_request_clock(clk->cg_sel, true);
	if (err)
		prcmu_request_ape_opp_100_voltage(false);

	return err;
}

static void clk_prcmu_opp_volt_unprepare(struct clk_hw *hw)
{
	struct clk_prcmu *clk = to_clk_prcmu(hw);

	if (prcmu_request_clock(clk->cg_sel, false))
		goto out_error;
	if (prcmu_request_ape_opp_100_voltage(false))
		goto out_error;
	return;

out_error:
	pr_err("clk_prcmu: %s failed to disable %s.\n", __func__,
		hw->init->name);
}

static struct clk_ops clk_prcmu_scalable_ops = {
	.prepare = clk_prcmu_prepare,
	.unprepare = clk_prcmu_unprepare,
	.enable = clk_prcmu_enable,
	.disable = clk_prcmu_disable,
	.is_enabled = clk_prcmu_is_enabled,
	.recalc_rate = clk_prcmu_recalc_rate,
	.round_rate = clk_prcmu_round_rate,
	.set_rate = clk_prcmu_set_rate,
};

static struct clk_ops clk_prcmu_gate_ops = {
	.prepare = clk_prcmu_prepare,
	.unprepare = clk_prcmu_unprepare,
	.enable = clk_prcmu_enable,
	.disable = clk_prcmu_disable,
	.is_enabled = clk_prcmu_is_enabled,
	.recalc_rate = clk_prcmu_recalc_rate,
};

static struct clk_ops clk_prcmu_rate_ops = {
	.is_enabled = clk_prcmu_is_enabled,
	.recalc_rate = clk_prcmu_recalc_rate,
};

static struct clk_ops clk_prcmu_opp_gate_ops = {
	.prepare = clk_prcmu_opp_prepare,
	.unprepare = clk_prcmu_opp_unprepare,
	.enable = clk_prcmu_enable,
	.disable = clk_prcmu_disable,
	.is_enabled = clk_prcmu_is_enabled,
	.recalc_rate = clk_prcmu_recalc_rate,
};

static struct clk_ops clk_prcmu_opp_volt_scalable_ops = {
	.prepare = clk_prcmu_opp_volt_prepare,
	.unprepare = clk_prcmu_opp_volt_unprepare,
	.enable = clk_prcmu_enable,
	.disable = clk_prcmu_disable,
	.is_enabled = clk_prcmu_is_enabled,
	.recalc_rate = clk_prcmu_recalc_rate,
	.round_rate = clk_prcmu_round_rate,
	.set_rate = clk_prcmu_set_rate,
};

static struct clk *clk_reg_prcmu(const char *name,
				 const char *parent_name,
				 u8 cg_sel,
				 unsigned long rate,
				 unsigned long flags,
				 struct clk_ops *clk_prcmu_ops)
{
	struct clk_prcmu *clk;
	struct clk_init_data clk_prcmu_init;
	struct clk *clk_reg;

	if (!name) {
		pr_err("clk_prcmu: %s invalid arguments passed\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	clk = kzalloc(sizeof(struct clk_prcmu), GFP_KERNEL);
	if (!clk) {
		pr_err("clk_prcmu: %s could not allocate clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	clk->cg_sel = cg_sel;
	clk->is_enabled = 1;
	/* "rate" can be used for changing the initial frequency */
	if (rate)
		prcmu_set_clock_rate(cg_sel, rate);

	clk_prcmu_init.name = name;
	clk_prcmu_init.ops = clk_prcmu_ops;
	clk_prcmu_init.flags = flags;
	clk_prcmu_init.parent_names = (parent_name ? &parent_name : NULL);
	clk_prcmu_init.num_parents = (parent_name ? 1 : 0);
	clk->hw.init = &clk_prcmu_init;

	clk_reg = clk_register(NULL, &clk->hw);
	if (IS_ERR_OR_NULL(clk_reg))
		goto free_clk;

	return clk_reg;

free_clk:
	kfree(clk);
	pr_err("clk_prcmu: %s failed to register clk\n", __func__);
	return ERR_PTR(-ENOMEM);
}

struct clk *clk_reg_prcmu_scalable(const char *name,
				   const char *parent_name,
				   u8 cg_sel,
				   unsigned long rate,
				   unsigned long flags)
{
	return clk_reg_prcmu(name, parent_name, cg_sel, rate, flags,
			&clk_prcmu_scalable_ops);
}

struct clk *clk_reg_prcmu_gate(const char *name,
			       const char *parent_name,
			       u8 cg_sel,
			       unsigned long flags)
{
	return clk_reg_prcmu(name, parent_name, cg_sel, 0, flags,
			&clk_prcmu_gate_ops);
}

struct clk *clk_reg_prcmu_rate(const char *name,
			       const char *parent_name,
			       u8 cg_sel,
			       unsigned long flags)
{
	return clk_reg_prcmu(name, parent_name, cg_sel, 0, flags,
			&clk_prcmu_rate_ops);
}

struct clk *clk_reg_prcmu_opp_gate(const char *name,
				   const char *parent_name,
				   u8 cg_sel,
				   unsigned long flags)
{
	return clk_reg_prcmu(name, parent_name, cg_sel, 0, flags,
			&clk_prcmu_opp_gate_ops);
}

struct clk *clk_reg_prcmu_opp_volt_scalable(const char *name,
					    const char *parent_name,
					    u8 cg_sel,
					    unsigned long rate,
					    unsigned long flags)
{
	return clk_reg_prcmu(name, parent_name, cg_sel, rate, flags,
			&clk_prcmu_opp_volt_scalable_ops);
}
