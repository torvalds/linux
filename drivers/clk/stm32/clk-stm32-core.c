// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2022 - All Rights Reserved
 * Author: Gabriel Fernandez <gabriel.fernandez@foss.st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "clk-stm32-core.h"
#include "reset-stm32.h"

static DEFINE_SPINLOCK(rlock);

static int stm32_rcc_clock_init(struct device *dev,
				const struct of_device_id *match,
				void __iomem *base)
{
	const struct stm32_rcc_match_data *data = match->data;
	struct clk_hw_onecell_data *clk_data = data->hw_clks;
	struct device_node *np = dev_of_node(dev);
	struct clk_hw **hws;
	int n, max_binding;

	max_binding =  data->maxbinding;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, max_binding), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = max_binding;

	hws = clk_data->hws;

	for (n = 0; n < max_binding; n++)
		hws[n] = ERR_PTR(-ENOENT);

	for (n = 0; n < data->num_clocks; n++) {
		const struct clock_config *cfg_clock = &data->tab_clocks[n];
		struct clk_hw *hw = ERR_PTR(-ENOENT);

		if (cfg_clock->func)
			hw = (*cfg_clock->func)(dev, data, base, &rlock,
						cfg_clock);

		if (IS_ERR(hw)) {
			dev_err(dev, "Can't register clk %d: %ld\n", n,
				PTR_ERR(hw));
			return PTR_ERR(hw);
		}

		if (cfg_clock->id != NO_ID)
			hws[cfg_clock->id] = hw;
	}

	return of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);
}

int stm32_rcc_init(struct device *dev, const struct of_device_id *match_data,
		   void __iomem *base)
{
	const struct of_device_id *match;
	int err;

	match = of_match_node(match_data, dev_of_node(dev));
	if (!match) {
		dev_err(dev, "match data not found\n");
		return -ENODEV;
	}

	/* RCC Reset Configuration */
	err = stm32_rcc_reset_init(dev, match, base);
	if (err) {
		pr_err("stm32 reset failed to initialize\n");
		return err;
	}

	/* RCC Clock Configuration */
	err = stm32_rcc_clock_init(dev, match, base);
	if (err) {
		pr_err("stm32 clock failed to initialize\n");
		return err;
	}

	return 0;
}

static u8 stm32_mux_get_parent(void __iomem *base,
			       struct clk_stm32_clock_data *data,
			       u16 mux_id)
{
	const struct stm32_mux_cfg *mux = &data->muxes[mux_id];
	u32 mask = BIT(mux->width) - 1;
	u32 val;

	val = readl(base + mux->offset) >> mux->shift;
	val &= mask;

	return val;
}

static int stm32_mux_set_parent(void __iomem *base,
				struct clk_stm32_clock_data *data,
				u16 mux_id, u8 index)
{
	const struct stm32_mux_cfg *mux = &data->muxes[mux_id];

	u32 mask = BIT(mux->width) - 1;
	u32 reg = readl(base + mux->offset);
	u32 val = index << mux->shift;

	reg &= ~(mask << mux->shift);
	reg |= val;

	writel(reg, base + mux->offset);

	return 0;
}

static void stm32_gate_endisable(void __iomem *base,
				 struct clk_stm32_clock_data *data,
				 u16 gate_id, int enable)
{
	const struct stm32_gate_cfg *gate = &data->gates[gate_id];
	void __iomem *addr = base + gate->offset;

	if (enable) {
		if (data->gate_cpt[gate_id]++ > 0)
			return;

		if (gate->set_clr != 0)
			writel(BIT(gate->bit_idx), addr);
		else
			writel(readl(addr) | BIT(gate->bit_idx), addr);
	} else {
		if (--data->gate_cpt[gate_id] > 0)
			return;

		if (gate->set_clr != 0)
			writel(BIT(gate->bit_idx), addr + gate->set_clr);
		else
			writel(readl(addr) & ~BIT(gate->bit_idx), addr);
	}
}

static void stm32_gate_disable_unused(void __iomem *base,
				      struct clk_stm32_clock_data *data,
				      u16 gate_id)
{
	const struct stm32_gate_cfg *gate = &data->gates[gate_id];
	void __iomem *addr = base + gate->offset;

	if (data->gate_cpt[gate_id] > 0)
		return;

	if (gate->set_clr != 0)
		writel(BIT(gate->bit_idx), addr + gate->set_clr);
	else
		writel(readl(addr) & ~BIT(gate->bit_idx), addr);
}

static int stm32_gate_is_enabled(void __iomem *base,
				 struct clk_stm32_clock_data *data,
				 u16 gate_id)
{
	const struct stm32_gate_cfg *gate = &data->gates[gate_id];

	return (readl(base + gate->offset) & BIT(gate->bit_idx)) != 0;
}

static u8 clk_stm32_mux_get_parent(struct clk_hw *hw)
{
	struct clk_stm32_mux *mux = to_clk_stm32_mux(hw);

	return stm32_mux_get_parent(mux->base, mux->clock_data, mux->mux_id);
}

static int clk_stm32_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_stm32_mux *mux = to_clk_stm32_mux(hw);
	unsigned long flags = 0;

	spin_lock_irqsave(mux->lock, flags);

	stm32_mux_set_parent(mux->base, mux->clock_data, mux->mux_id, index);

	spin_unlock_irqrestore(mux->lock, flags);

	return 0;
}

const struct clk_ops clk_stm32_mux_ops = {
	.get_parent	= clk_stm32_mux_get_parent,
	.set_parent	= clk_stm32_mux_set_parent,
};

static void clk_stm32_gate_endisable(struct clk_hw *hw, int enable)
{
	struct clk_stm32_gate *gate = to_clk_stm32_gate(hw);
	unsigned long flags = 0;

	spin_lock_irqsave(gate->lock, flags);

	stm32_gate_endisable(gate->base, gate->clock_data, gate->gate_id, enable);

	spin_unlock_irqrestore(gate->lock, flags);
}

static int clk_stm32_gate_enable(struct clk_hw *hw)
{
	clk_stm32_gate_endisable(hw, 1);

	return 0;
}

static void clk_stm32_gate_disable(struct clk_hw *hw)
{
	clk_stm32_gate_endisable(hw, 0);
}

static int clk_stm32_gate_is_enabled(struct clk_hw *hw)
{
	struct clk_stm32_gate *gate = to_clk_stm32_gate(hw);

	return stm32_gate_is_enabled(gate->base, gate->clock_data, gate->gate_id);
}

static void clk_stm32_gate_disable_unused(struct clk_hw *hw)
{
	struct clk_stm32_gate *gate = to_clk_stm32_gate(hw);
	unsigned long flags = 0;

	spin_lock_irqsave(gate->lock, flags);

	stm32_gate_disable_unused(gate->base, gate->clock_data, gate->gate_id);

	spin_unlock_irqrestore(gate->lock, flags);
}

const struct clk_ops clk_stm32_gate_ops = {
	.enable		= clk_stm32_gate_enable,
	.disable	= clk_stm32_gate_disable,
	.is_enabled	= clk_stm32_gate_is_enabled,
	.disable_unused	= clk_stm32_gate_disable_unused,
};

struct clk_hw *clk_stm32_mux_register(struct device *dev,
				      const struct stm32_rcc_match_data *data,
				      void __iomem *base,
				      spinlock_t *lock,
				      const struct clock_config *cfg)
{
	struct clk_stm32_mux *mux = cfg->clock_cfg;
	struct clk_hw *hw = &mux->hw;
	int err;

	mux->base = base;
	mux->lock = lock;
	mux->clock_data = data->clock_data;

	err = clk_hw_register(dev, hw);
	if (err)
		return ERR_PTR(err);

	return hw;
}

struct clk_hw *clk_stm32_gate_register(struct device *dev,
				       const struct stm32_rcc_match_data *data,
				       void __iomem *base,
				       spinlock_t *lock,
				       const struct clock_config *cfg)
{
	struct clk_stm32_gate *gate = cfg->clock_cfg;
	struct clk_hw *hw = &gate->hw;
	int err;

	gate->base = base;
	gate->lock = lock;
	gate->clock_data = data->clock_data;

	err = clk_hw_register(dev, hw);
	if (err)
		return ERR_PTR(err);

	return hw;
}
