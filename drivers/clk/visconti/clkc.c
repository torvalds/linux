// SPDX-License-Identifier: GPL-2.0-only
/*
 * Toshiba Visconti clock controller
 *
 * Copyright (c) 2021 TOSHIBA CORPORATION
 * Copyright (c) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "clkc.h"

static inline struct visconti_clk_gate *to_visconti_clk_gate(struct clk_hw *hw)
{
	return container_of(hw, struct visconti_clk_gate, hw);
}

static int visconti_gate_clk_is_enabled(struct clk_hw *hw)
{
	struct visconti_clk_gate *gate = to_visconti_clk_gate(hw);
	u32 clk = BIT(gate->ck_idx);
	u32 val;

	regmap_read(gate->regmap, gate->ckon_offset, &val);
	return (val & clk) ? 1 : 0;
}

static void visconti_gate_clk_disable(struct clk_hw *hw)
{
	struct visconti_clk_gate *gate = to_visconti_clk_gate(hw);
	u32 clk = BIT(gate->ck_idx);
	unsigned long flags;

	spin_lock_irqsave(gate->lock, flags);

	if (!visconti_gate_clk_is_enabled(hw)) {
		spin_unlock_irqrestore(gate->lock, flags);
		return;
	}

	regmap_update_bits(gate->regmap, gate->ckoff_offset, clk, clk);
	spin_unlock_irqrestore(gate->lock, flags);
}

static int visconti_gate_clk_enable(struct clk_hw *hw)
{
	struct visconti_clk_gate *gate = to_visconti_clk_gate(hw);
	u32 clk = BIT(gate->ck_idx);
	unsigned long flags;

	spin_lock_irqsave(gate->lock, flags);
	regmap_update_bits(gate->regmap, gate->ckon_offset, clk, clk);
	spin_unlock_irqrestore(gate->lock, flags);

	return 0;
}

static const struct clk_ops visconti_clk_gate_ops = {
	.enable = visconti_gate_clk_enable,
	.disable = visconti_gate_clk_disable,
	.is_enabled = visconti_gate_clk_is_enabled,
};

static struct clk_hw *visconti_clk_register_gate(struct device *dev,
						 const char *name,
						 const char *parent_name,
						 struct regmap *regmap,
						 const struct visconti_clk_gate_table *clks,
						 u32	rson_offset,
						 u32	rsoff_offset,
						 u8	rs_idx,
						 spinlock_t *lock)
{
	struct visconti_clk_gate *gate;
	struct clk_parent_data *pdata;
	struct clk_init_data init;
	struct clk_hw *hw;
	int ret;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->name = pdata->fw_name = parent_name;

	gate = devm_kzalloc(dev, sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &visconti_clk_gate_ops;
	init.flags = clks->flags;
	init.parent_data = pdata;
	init.num_parents = 1;

	gate->regmap = regmap;
	gate->ckon_offset = clks->ckon_offset;
	gate->ckoff_offset = clks->ckoff_offset;
	gate->ck_idx = clks->ck_idx;
	gate->rson_offset = rson_offset;
	gate->rsoff_offset = rsoff_offset;
	gate->rs_idx = rs_idx;
	gate->lock = lock;
	gate->hw.init = &init;

	hw = &gate->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		hw = ERR_PTR(ret);

	return hw;
}

int visconti_clk_register_gates(struct visconti_clk_provider *ctx,
				const struct visconti_clk_gate_table *clks,
				int num_gate,
				const struct visconti_reset_data *reset,
				spinlock_t *lock)
{
	struct device *dev = ctx->dev;
	int i;

	for (i = 0; i < num_gate; i++) {
		const char *parent_div_name = clks[i].parent_data[0].name;
		struct clk_parent_data *pdata;
		u32 rson_offset, rsoff_offset;
		struct clk_hw *gate_clk;
		struct clk_hw *div_clk;
		char *dev_name;
		u8 rs_idx;

		pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		dev_name = devm_kasprintf(dev, GFP_KERNEL, "%s_div", clks[i].name);
		if (!dev_name)
			return -ENOMEM;

		if (clks[i].rs_id != NO_RESET) {
			rson_offset = reset[clks[i].rs_id].rson_offset;
			rsoff_offset = reset[clks[i].rs_id].rsoff_offset;
			rs_idx = reset[clks[i].rs_id].rs_idx;
		} else {
			rson_offset = rsoff_offset = rs_idx = -1;
		}

		div_clk = devm_clk_hw_register_fixed_factor(dev,
							    dev_name,
							    parent_div_name,
							    0, 1,
							    clks[i].div);
		if (IS_ERR(div_clk))
			return PTR_ERR(div_clk);

		gate_clk = visconti_clk_register_gate(dev,
						      clks[i].name,
						      dev_name,
						      ctx->regmap,
						      &clks[i],
						      rson_offset,
						      rsoff_offset,
						      rs_idx,
						      lock);
		if (IS_ERR(gate_clk)) {
			dev_err(dev, "%s: failed to register clock %s\n",
				__func__, clks[i].name);
			return PTR_ERR(gate_clk);
		}

		ctx->clk_data.hws[clks[i].id] = gate_clk;
	}

	return 0;
}

struct visconti_clk_provider *visconti_init_clk(struct device *dev,
						struct regmap *regmap,
						unsigned long nr_clks)
{
	struct visconti_clk_provider *ctx;
	int i;

	ctx = devm_kzalloc(dev, struct_size(ctx, clk_data.hws, nr_clks), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < nr_clks; ++i)
		ctx->clk_data.hws[i] = ERR_PTR(-ENOENT);
	ctx->clk_data.num = nr_clks;

	ctx->dev = dev;
	ctx->regmap = regmap;

	return ctx;
}
