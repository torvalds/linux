// SPDX-License-Identifier: GPL-2.0-only
/*
 * Microchip Image Sensor Controller (ISC) common clock driver setup
 *
 * Copyright (C) 2016 Microchip Technology, Inc.
 *
 * Author: Songjun Wu
 * Author: Eugen Hristev <eugen.hristev@microchip.com>
 *
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include "microchip-isc-regs.h"
#include "microchip-isc.h"

static int isc_wait_clk_stable(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	struct regmap *regmap = isc_clk->regmap;
	unsigned long timeout = jiffies + usecs_to_jiffies(1000);
	unsigned int status;

	while (time_before(jiffies, timeout)) {
		regmap_read(regmap, ISC_CLKSR, &status);
		if (!(status & ISC_CLKSR_SIP))
			return 0;

		usleep_range(10, 250);
	}

	return -ETIMEDOUT;
}

static int isc_clk_prepare(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	int ret;

	ret = pm_runtime_resume_and_get(isc_clk->dev);
	if (ret < 0)
		return ret;

	return isc_wait_clk_stable(hw);
}

static void isc_clk_unprepare(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);

	isc_wait_clk_stable(hw);

	pm_runtime_put_sync(isc_clk->dev);
}

static int isc_clk_enable(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	u32 id = isc_clk->id;
	struct regmap *regmap = isc_clk->regmap;
	unsigned long flags;
	unsigned int status;

	dev_dbg(isc_clk->dev, "ISC CLK: %s, id = %d, div = %d, parent id = %d\n",
		__func__, id, isc_clk->div, isc_clk->parent_id);

	spin_lock_irqsave(&isc_clk->lock, flags);
	regmap_update_bits(regmap, ISC_CLKCFG,
			   ISC_CLKCFG_DIV_MASK(id) | ISC_CLKCFG_SEL_MASK(id),
			   (isc_clk->div << ISC_CLKCFG_DIV_SHIFT(id)) |
			   (isc_clk->parent_id << ISC_CLKCFG_SEL_SHIFT(id)));

	regmap_write(regmap, ISC_CLKEN, ISC_CLK(id));
	spin_unlock_irqrestore(&isc_clk->lock, flags);

	regmap_read(regmap, ISC_CLKSR, &status);
	if (status & ISC_CLK(id))
		return 0;
	else
		return -EINVAL;
}

static void isc_clk_disable(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	u32 id = isc_clk->id;
	unsigned long flags;

	spin_lock_irqsave(&isc_clk->lock, flags);
	regmap_write(isc_clk->regmap, ISC_CLKDIS, ISC_CLK(id));
	spin_unlock_irqrestore(&isc_clk->lock, flags);
}

static int isc_clk_is_enabled(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	u32 status;
	int ret;

	ret = pm_runtime_resume_and_get(isc_clk->dev);
	if (ret < 0)
		return 0;

	regmap_read(isc_clk->regmap, ISC_CLKSR, &status);

	pm_runtime_put_sync(isc_clk->dev);

	return status & ISC_CLK(isc_clk->id) ? 1 : 0;
}

static unsigned long
isc_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);

	return DIV_ROUND_CLOSEST(parent_rate, isc_clk->div + 1);
}

static int isc_clk_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	long best_rate = -EINVAL;
	int best_diff = -1;
	unsigned int i, div;

	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		struct clk_hw *parent;
		unsigned long parent_rate;

		parent = clk_hw_get_parent_by_index(hw, i);
		if (!parent)
			continue;

		parent_rate = clk_hw_get_rate(parent);
		if (!parent_rate)
			continue;

		for (div = 1; div < ISC_CLK_MAX_DIV + 2; div++) {
			unsigned long rate;
			int diff;

			rate = DIV_ROUND_CLOSEST(parent_rate, div);
			diff = abs(req->rate - rate);

			if (best_diff < 0 || best_diff > diff) {
				best_rate = rate;
				best_diff = diff;
				req->best_parent_rate = parent_rate;
				req->best_parent_hw = parent;
			}

			if (!best_diff || rate < req->rate)
				break;
		}

		if (!best_diff)
			break;
	}

	dev_dbg(isc_clk->dev,
		"ISC CLK: %s, best_rate = %ld, parent clk: %s @ %ld\n",
		__func__, best_rate,
		__clk_get_name((req->best_parent_hw)->clk),
		req->best_parent_rate);

	if (best_rate < 0)
		return best_rate;

	req->rate = best_rate;

	return 0;
}

static int isc_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);

	if (index >= clk_hw_get_num_parents(hw))
		return -EINVAL;

	isc_clk->parent_id = index;

	return 0;
}

static u8 isc_clk_get_parent(struct clk_hw *hw)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);

	return isc_clk->parent_id;
}

static int isc_clk_set_rate(struct clk_hw *hw,
			    unsigned long rate,
			    unsigned long parent_rate)
{
	struct isc_clk *isc_clk = to_isc_clk(hw);
	u32 div;

	if (!rate)
		return -EINVAL;

	div = DIV_ROUND_CLOSEST(parent_rate, rate);
	if (div > (ISC_CLK_MAX_DIV + 1) || !div)
		return -EINVAL;

	isc_clk->div = div - 1;

	return 0;
}

static const struct clk_ops isc_clk_ops = {
	.prepare	= isc_clk_prepare,
	.unprepare	= isc_clk_unprepare,
	.enable		= isc_clk_enable,
	.disable	= isc_clk_disable,
	.is_enabled	= isc_clk_is_enabled,
	.recalc_rate	= isc_clk_recalc_rate,
	.determine_rate	= isc_clk_determine_rate,
	.set_parent	= isc_clk_set_parent,
	.get_parent	= isc_clk_get_parent,
	.set_rate	= isc_clk_set_rate,
};

static int isc_clk_register(struct isc_device *isc, unsigned int id)
{
	struct regmap *regmap = isc->regmap;
	struct device_node *np = isc->dev->of_node;
	struct isc_clk *isc_clk;
	struct clk_init_data init;
	const char *clk_name = np->name;
	const char *parent_names[3];
	int num_parents;

	if (id == ISC_ISPCK && !isc->ispck_required)
		return 0;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents < 1 || num_parents > 3)
		return -EINVAL;

	if (num_parents > 2 && id == ISC_ISPCK)
		num_parents = 2;

	of_clk_parent_fill(np, parent_names, num_parents);

	if (id == ISC_MCK)
		of_property_read_string(np, "clock-output-names", &clk_name);
	else
		clk_name = "isc-ispck";

	init.parent_names	= parent_names;
	init.num_parents	= num_parents;
	init.name		= clk_name;
	init.ops		= &isc_clk_ops;
	init.flags		= CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE;

	isc_clk = &isc->isc_clks[id];
	isc_clk->hw.init	= &init;
	isc_clk->regmap		= regmap;
	isc_clk->id		= id;
	isc_clk->dev		= isc->dev;
	spin_lock_init(&isc_clk->lock);

	isc_clk->clk = clk_register(isc->dev, &isc_clk->hw);
	if (IS_ERR(isc_clk->clk)) {
		dev_err(isc->dev, "%s: clock register fail\n", clk_name);
		return PTR_ERR(isc_clk->clk);
	} else if (id == ISC_MCK) {
		of_clk_add_provider(np, of_clk_src_simple_get, isc_clk->clk);
	}

	return 0;
}

int microchip_isc_clk_init(struct isc_device *isc)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(isc->isc_clks); i++)
		isc->isc_clks[i].clk = ERR_PTR(-EINVAL);

	for (i = 0; i < ARRAY_SIZE(isc->isc_clks); i++) {
		ret = isc_clk_register(isc, i);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(microchip_isc_clk_init);

void microchip_isc_clk_cleanup(struct isc_device *isc)
{
	unsigned int i;

	of_clk_del_provider(isc->dev->of_node);

	for (i = 0; i < ARRAY_SIZE(isc->isc_clks); i++) {
		struct isc_clk *isc_clk = &isc->isc_clks[i];

		if (!IS_ERR(isc_clk->clk))
			clk_unregister(isc_clk->clk);
	}
}
EXPORT_SYMBOL_GPL(microchip_isc_clk_cleanup);
