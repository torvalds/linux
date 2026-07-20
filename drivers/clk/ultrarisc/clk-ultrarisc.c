// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 UltraRISC Technology (Shanghai) Co., Ltd.
 */

#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "clk-ultrarisc.h"

struct ultrarisc_pll_clk {
	struct clk_hw hw;
	void __iomem *base;
	const struct ultrarisc_pll_layout *layout;
};

struct ultrarisc_divider_clk {
	struct clk_divider divider;
	struct clk_gate gate;
	u32 load_mask;
};

#define to_ultrarisc_pll_clk(_hw) \
	container_of(_hw, struct ultrarisc_pll_clk, hw)

static inline struct ultrarisc_divider_clk *to_ultrarisc_divider_clk(struct clk_hw *hw)
{
	struct clk_divider *divider = to_clk_divider(hw);

	return container_of(divider, struct ultrarisc_divider_clk, divider);
}

static unsigned long ultrarisc_pll_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct ultrarisc_pll_clk *pll = to_ultrarisc_pll_clk(hw);
	const struct ultrarisc_pll_layout *layout = pll->layout;
	u32 oddiv1_div, oddiv2_div;
	u64 mult, rate, den;
	u32 frac, m, n;
	u32 cfg1, cfg2;

	cfg1 = readl_relaxed(pll->base + layout->cfg1_offset);
	cfg2 = readl_relaxed(pll->base + layout->cfg2_offset);

	frac = field_get(layout->frac_mask, cfg1);
	m = field_get(layout->m_mask, cfg2);
	n = field_get(layout->n_mask, cfg2);
	if (!n)
		return 0;

	oddiv1_div = 1U << field_get(layout->oddiv1_mask, cfg2);
	oddiv2_div = 1U << field_get(layout->oddiv2_mask, cfg2);

	/*
	 * The output frequency is calculated as:
	 * fvco = parent * (m + frac / 2^24) / n
	 * fout = fvco / (2^oddiv1_raw * 2^oddiv2_raw)
	 *
	 * The output divider values are derived from the raw register field values as:
	 * oddivX_div = 1 << oddivX_raw
	 */
	mult = ((u64)m << 24) + frac;
	rate = (u64)parent_rate * mult;
	den = ((u64)n << 24) * oddiv1_div * oddiv2_div;

	return div64_u64(rate + (den >> 1), den);
}

static const struct clk_ops ultrarisc_pll_ro_ops = {
	.recalc_rate = ultrarisc_pll_recalc_rate,
};

static unsigned long ultrarisc_divider_recalc_rate(struct clk_hw *hw,
						   unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	u32 val;

	val = readl_relaxed(divider->reg) >> divider->shift;
	val &= clk_div_mask(divider->width);

	return divider_recalc_rate(hw, parent_rate, val, divider->table,
				   divider->flags, divider->width);
}

static int ultrarisc_divider_determine_rate(struct clk_hw *hw,
					    struct clk_rate_request *req)
{
	struct clk_divider *divider = to_clk_divider(hw);

	return divider_determine_rate(hw, req, divider->table, divider->width,
				      divider->flags);
}

static int ultrarisc_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long parent_rate)
{
	struct ultrarisc_divider_clk *divider_clk = to_ultrarisc_divider_clk(hw);
	struct clk_divider *divider = &divider_clk->divider;
	int value;
	u32 val;

	value = divider_get_val(rate, parent_rate, divider->table,
				divider->width, divider->flags);
	if (value < 0)
		return value;

	scoped_guard(spinlock_irqsave, divider->lock) {
		val = readl_relaxed(divider->reg);
		val &= ~(clk_div_mask(divider->width) << divider->shift);
		val |= value << divider->shift;
		writel_relaxed(val, divider->reg);

		if (divider_clk->load_mask) {
			/*
			 * Program the new divider field, then write 1 to the
			 * load bit to trigger the update. The load bit is
			 * write-triggered and reads back as 0 on this hardware.
			 */
			writel_relaxed(val | divider_clk->load_mask, divider->reg);
		}
	}

	return 0;
}

static const struct clk_ops ultrarisc_divider_ops = {
	.recalc_rate = ultrarisc_divider_recalc_rate,
	.determine_rate = ultrarisc_divider_determine_rate,
	.set_rate = ultrarisc_divider_set_rate,
};

static struct clk_hw *ultrarisc_clk_register_pll(struct device *dev,
						 const struct ultrarisc_pll_desc *desc,
						 const struct ultrarisc_pll_layout *layout,
						 void __iomem *base)
{
	struct clk_parent_data pdata = { .index = 0 };
	struct ultrarisc_pll_clk *pll;
	struct clk_init_data init = {
		.name = desc->name,
		.ops = &ultrarisc_pll_ro_ops,
		.parent_data = &pdata,
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE,
	};
	int ret;

	pll = devm_kzalloc(dev, sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->base = base;
	pll->layout = layout;
	pll->hw.init = &init;

	ret = devm_clk_hw_register(dev, &pll->hw);
	if (ret)
		return ERR_PTR(ret);

	return &pll->hw;
}

static struct clk_hw *
ultrarisc_clk_register_divider(struct device *dev,
			       const struct ultrarisc_divider_desc *desc,
			       struct clk_hw *parent_hw, void __iomem *base,
			       spinlock_t *lock)
{
	const struct clk_parent_data pdata = { .hw = parent_hw };
	void __iomem *reg = base + desc->offset;
	struct ultrarisc_divider_clk *divider;

	if (!desc->div_width)
		return ERR_PTR(-EINVAL);

	if (!lock)
		return ERR_PTR(-EINVAL);

	divider = devm_kzalloc(dev, sizeof(*divider), GFP_KERNEL);
	if (!divider)
		return ERR_PTR(-ENOMEM);

	divider->divider.reg = reg;
	divider->divider.shift = desc->div_shift;
	divider->divider.width = desc->div_width;
	divider->divider.flags = desc->divider_flags;
	divider->divider.lock = lock;
	divider->load_mask = desc->load_mask;
	divider->gate.reg = reg;
	divider->gate.bit_idx = desc->gate_bit;
	divider->gate.flags = desc->gate_flags;
	divider->gate.lock = lock;

	return devm_clk_hw_register_composite_pdata(dev, desc->name,
						    &pdata, 1, NULL, NULL,
						    &divider->divider.hw,
						    &ultrarisc_divider_ops,
						    &divider->gate.hw,
						    &clk_gate_ops, 0);
}

static int ultrarisc_clk_register_fixed_factors(struct device *dev,
						struct clk_hw_onecell_data *clk_data,
						const struct ultrarisc_clk_soc_data *soc_data)
{
	u32 i;

	for (i = 0; i < soc_data->num_fixed_factors; i++) {
		const struct ultrarisc_fixed_factor_desc *desc;
		struct clk_hw *parent_hw;
		struct clk_hw *hw;

		desc = &soc_data->fixed_factors[i];
		if (desc->id >= clk_data->num || desc->parent_id >= clk_data->num)
			return -EINVAL;

		parent_hw = clk_data->hws[desc->parent_id];
		if (!parent_hw)
			return -EINVAL;

		hw = devm_clk_hw_register_fixed_factor_parent_hw(dev, desc->name,
								 parent_hw, 0,
								 desc->mult,
								 desc->div);
		if (IS_ERR(hw))
			return PTR_ERR(hw);

		clk_data->hws[desc->id] = hw;
	}

	return 0;
}

static int ultrarisc_clk_register_plls(struct platform_device *pdev,
				       struct clk_hw_onecell_data *clk_data,
				       const struct ultrarisc_clk_soc_data *soc_data,
				       void __iomem *base)
{
	struct device *dev = &pdev->dev;
	u32 i;

	for (i = 0; i < soc_data->num_plls; i++) {
		const struct ultrarisc_pll_desc *desc = &soc_data->plls[i];
		struct clk_hw *hw;

		if (desc->id >= clk_data->num) {
			dev_err(dev, "%s invalid clock ID %u >= %u\n",
				desc->name, desc->id, clk_data->num);
			return -EINVAL;
		}

		hw = ultrarisc_clk_register_pll(dev, desc, soc_data->pll_layout, base);
		if (IS_ERR(hw))
			return PTR_ERR(hw);

		clk_data->hws[desc->id] = hw;
	}

	return 0;
}

static int ultrarisc_clk_register_dividers(struct platform_device *pdev,
					   struct clk_hw_onecell_data *clk_data,
					   const struct ultrarisc_clk_soc_data *soc_data,
					   void __iomem *base,
					   spinlock_t *lock)
{
	struct device *dev = &pdev->dev;
	u32 i;

	for (i = 0; i < soc_data->num_dividers; i++) {
		const struct ultrarisc_divider_desc *desc;
		struct clk_hw *parent_hw;
		struct clk_hw *hw;

		desc = &soc_data->dividers[i];
		if (desc->id >= clk_data->num || desc->parent_id >= clk_data->num)
			return -EINVAL;

		parent_hw = clk_data->hws[desc->parent_id];
		if (!parent_hw)
			return -EINVAL;

		hw = ultrarisc_clk_register_divider(dev, desc, parent_hw, base,
						    lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);

		if (desc->max_rate)
			clk_hw_set_rate_range(hw, 0, desc->max_rate);

		clk_data->hws[desc->id] = hw;
	}

	return 0;
}

static int ultrarisc_clk_register_gates(struct platform_device *pdev,
					struct clk_hw_onecell_data *clk_data,
					const struct ultrarisc_clk_soc_data *soc_data,
					void __iomem *base,
					spinlock_t *lock)
{
	struct device *dev = &pdev->dev;
	u32 i;

	for (i = 0; i < soc_data->num_gates; i++) {
		const struct ultrarisc_gate_desc *desc;
		struct clk_hw *parent_hw;
		struct clk_hw *hw;

		desc = &soc_data->gates[i];
		if (desc->id >= clk_data->num || desc->parent_id >= clk_data->num)
			return -EINVAL;

		parent_hw = clk_data->hws[desc->parent_id];
		if (!parent_hw)
			return -EINVAL;

		hw = devm_clk_hw_register_gate_parent_hw(dev, desc->name,
							 parent_hw, 0,
							 base + desc->offset,
							 desc->gate_bit,
							 desc->gate_flags,
							 lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);

		clk_data->hws[desc->id] = hw;
	}

	return 0;
}

int ultrarisc_clk_probe(struct platform_device *pdev,
			const struct ultrarisc_clk_soc_data *soc_data)
{
	struct clk_hw_onecell_data *clk_data;
	struct device *dev = &pdev->dev;
	void __iomem *base;
	spinlock_t *lock;
	int ret;

	if (!soc_data)
		return -EINVAL;

	lock = devm_kzalloc(dev, sizeof(*lock), GFP_KERNEL);
	if (!lock)
		return -ENOMEM;

	spin_lock_init(lock);

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws,
						 soc_data->num_clks),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = soc_data->num_clks;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ret = ultrarisc_clk_register_plls(pdev, clk_data, soc_data, base);
	if (ret)
		return ret;

	ret = ultrarisc_clk_register_fixed_factors(dev, clk_data, soc_data);
	if (ret)
		return ret;

	ret = ultrarisc_clk_register_dividers(pdev, clk_data, soc_data, base, lock);
	if (ret)
		return ret;

	ret = ultrarisc_clk_register_gates(pdev, clk_data, soc_data, base, lock);
	if (ret)
		return ret;

	for (int i = 0; i < clk_data->num; i++) {
		if (!clk_data->hws[i]) {
			dev_err(dev, "missing clock ID %u\n", i);
			return -EINVAL;
		}
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
}
EXPORT_SYMBOL_NS_GPL(ultrarisc_clk_probe, "CLK_ULTRARISC");

MODULE_DESCRIPTION("UltraRISC clock core driver");
MODULE_LICENSE("GPL");
