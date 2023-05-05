// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Edward-JW Yang <edward-jw.yang@mediatek.com>
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clkdev.h>
#include <linux/delay.h>

#include "clk-mtk.h"
#include "clk-pllfh.h"
#include "clk-fhctl.h"

static DEFINE_SPINLOCK(pllfh_lock);

inline struct mtk_fh *to_mtk_fh(struct clk_hw *hw)
{
	struct mtk_clk_pll *pll = to_mtk_clk_pll(hw);

	return container_of(pll, struct mtk_fh, clk_pll);
}

static int mtk_fhctl_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct mtk_clk_pll *pll = to_mtk_clk_pll(hw);
	struct mtk_fh *fh = to_mtk_fh(hw);
	u32 pcw = 0;
	u32 postdiv;

	mtk_pll_calc_values(pll, &pcw, &postdiv, rate, parent_rate);

	return fh->ops->hopping(fh, pcw, postdiv);
}

static const struct clk_ops mtk_pllfh_ops = {
	.is_prepared	= mtk_pll_is_prepared,
	.prepare	= mtk_pll_prepare,
	.unprepare	= mtk_pll_unprepare,
	.recalc_rate	= mtk_pll_recalc_rate,
	.round_rate	= mtk_pll_round_rate,
	.set_rate	= mtk_fhctl_set_rate,
};

static struct mtk_pllfh_data *get_pllfh_by_id(struct mtk_pllfh_data *pllfhs,
					      int num_fhs, int pll_id)
{
	int i;

	for (i = 0; i < num_fhs; i++)
		if (pllfhs[i].data.pll_id == pll_id)
			return &pllfhs[i];

	return NULL;
}

void fhctl_parse_dt(const u8 *compatible_node, struct mtk_pllfh_data *pllfhs,
		    int num_fhs)
{
	void __iomem *base;
	struct device_node *node;
	u32 num_clocks, pll_id, ssc_rate;
	int offset, i;

	node = of_find_compatible_node(NULL, NULL, compatible_node);
	if (!node) {
		pr_err("cannot find \"%s\"\n", compatible_node);
		return;
	}

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%s(): ioremap failed\n", __func__);
		goto out_node_put;
	}

	num_clocks = of_clk_get_parent_count(node);
	if (!num_clocks) {
		pr_err("%s(): failed to get clocks property\n", __func__);
		goto err;
	}

	for (i = 0; i < num_clocks; i++) {
		struct mtk_pllfh_data *pllfh;

		offset = i * 2;

		of_property_read_u32_index(node, "clocks", offset + 1, &pll_id);
		of_property_read_u32_index(node,
					   "mediatek,hopping-ssc-percent",
					   i, &ssc_rate);

		pllfh = get_pllfh_by_id(pllfhs, num_fhs, pll_id);
		if (!pllfh)
			continue;

		pllfh->state.fh_enable = 1;
		pllfh->state.ssc_rate = ssc_rate;
		pllfh->state.base = base;
	}

out_node_put:
	of_node_put(node);
	return;
err:
	iounmap(base);
	goto out_node_put;
}
EXPORT_SYMBOL_GPL(fhctl_parse_dt);

static int pllfh_init(struct mtk_fh *fh, struct mtk_pllfh_data *pllfh_data)
{
	struct fh_pll_regs *regs = &fh->regs;
	const struct fhctl_offset *offset;
	void __iomem *base = pllfh_data->state.base;
	void __iomem *fhx_base = base + pllfh_data->data.fhx_offset;

	offset = fhctl_get_offset_table(pllfh_data->data.fh_ver);
	if (IS_ERR(offset))
		return PTR_ERR(offset);

	regs->reg_hp_en = base + offset->offset_hp_en;
	regs->reg_clk_con = base + offset->offset_clk_con;
	regs->reg_rst_con = base + offset->offset_rst_con;
	regs->reg_slope0 = base + offset->offset_slope0;
	regs->reg_slope1 = base + offset->offset_slope1;

	regs->reg_cfg = fhx_base + offset->offset_cfg;
	regs->reg_updnlmt = fhx_base + offset->offset_updnlmt;
	regs->reg_dds = fhx_base + offset->offset_dds;
	regs->reg_dvfs = fhx_base + offset->offset_dvfs;
	regs->reg_mon = fhx_base + offset->offset_mon;

	fh->pllfh_data = pllfh_data;
	fh->lock = &pllfh_lock;

	fh->ops = fhctl_get_ops();

	return 0;
}

static bool fhctl_is_supported_and_enabled(const struct mtk_pllfh_data *pllfh)
{
	return pllfh && (pllfh->state.fh_enable == 1);
}

static struct clk_hw *
mtk_clk_register_pllfh(const struct mtk_pll_data *pll_data,
		       struct mtk_pllfh_data *pllfh_data, void __iomem *base)
{
	struct clk_hw *hw;
	struct mtk_fh *fh;
	int ret;

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (!fh)
		return ERR_PTR(-ENOMEM);

	ret = pllfh_init(fh, pllfh_data);
	if (ret) {
		hw = ERR_PTR(ret);
		goto out;
	}

	hw = mtk_clk_register_pll_ops(&fh->clk_pll, pll_data, base,
				      &mtk_pllfh_ops);

	if (IS_ERR(hw))
		goto out;

	fhctl_hw_init(fh);

out:
	if (IS_ERR(hw))
		kfree(fh);

	return hw;
}

static void mtk_clk_unregister_pllfh(struct clk_hw *hw)
{
	struct mtk_fh *fh;

	if (!hw)
		return;

	fh = to_mtk_fh(hw);

	clk_hw_unregister(hw);
	kfree(fh);
}

int mtk_clk_register_pllfhs(struct device_node *node,
			    const struct mtk_pll_data *plls, int num_plls,
			    struct mtk_pllfh_data *pllfhs, int num_fhs,
			    struct clk_hw_onecell_data *clk_data)
{
	void __iomem *base;
	int i;
	struct clk_hw *hw;

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%s(): ioremap failed\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < num_plls; i++) {
		const struct mtk_pll_data *pll = &plls[i];
		struct mtk_pllfh_data *pllfh;
		bool use_fhctl;

		pllfh = get_pllfh_by_id(pllfhs, num_fhs, pll->id);
		use_fhctl = fhctl_is_supported_and_enabled(pllfh);

		if (use_fhctl)
			hw = mtk_clk_register_pllfh(pll, pllfh, base);
		else
			hw = mtk_clk_register_pll(pll, base);

		if (IS_ERR(hw)) {
			pr_err("Failed to register %s clk %s: %ld\n",
			       use_fhctl ? "fhpll" : "pll", pll->name,
			       PTR_ERR(hw));
			goto err;
		}

		clk_data->hws[pll->id] = hw;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_pll_data *pll = &plls[i];
		struct mtk_pllfh_data *pllfh;
		bool use_fhctl;

		pllfh = get_pllfh_by_id(pllfhs, num_fhs, pll->id);
		use_fhctl = fhctl_is_supported_and_enabled(pllfh);

		if (use_fhctl)
			mtk_clk_unregister_pllfh(clk_data->hws[pll->id]);
		else
			mtk_clk_unregister_pll(clk_data->hws[pll->id]);

		clk_data->hws[pll->id] = ERR_PTR(-ENOENT);
	}

	iounmap(base);

	return PTR_ERR(hw);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_pllfhs);

void mtk_clk_unregister_pllfhs(const struct mtk_pll_data *plls, int num_plls,
			       struct mtk_pllfh_data *pllfhs, int num_fhs,
			       struct clk_hw_onecell_data *clk_data)
{
	void __iomem *base = NULL, *fhctl_base = NULL;
	int i;

	if (!clk_data)
		return;

	for (i = num_plls; i > 0; i--) {
		const struct mtk_pll_data *pll = &plls[i - 1];
		struct mtk_pllfh_data *pllfh;
		bool use_fhctl;

		if (IS_ERR_OR_NULL(clk_data->hws[pll->id]))
			continue;

		pllfh = get_pllfh_by_id(pllfhs, num_fhs, pll->id);
		use_fhctl = fhctl_is_supported_and_enabled(pllfh);

		if (use_fhctl) {
			fhctl_base = pllfh->state.base;
			mtk_clk_unregister_pllfh(clk_data->hws[pll->id]);
		} else {
			base = mtk_clk_pll_get_base(clk_data->hws[pll->id],
						    pll);
			mtk_clk_unregister_pll(clk_data->hws[pll->id]);
		}

		clk_data->hws[pll->id] = ERR_PTR(-ENOENT);
	}

	if (fhctl_base)
		iounmap(fhctl_base);

	iounmap(base);
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_pllfhs);
