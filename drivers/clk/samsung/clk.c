// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Copyright (c) 2013 Linaro Ltd.
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * This file includes utility functions to register clocks to common
 * clock framework for Samsung platforms.
 */

#include <linux/slab.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/syscore_ops.h>

#include "clk.h"

static LIST_HEAD(clock_reg_cache_list);

void samsung_clk_save(void __iomem *base,
				    struct regmap *regmap,
				    struct samsung_clk_reg_dump *rd,
				    unsigned int num_regs)
{
	for (; num_regs > 0; --num_regs, ++rd) {
		if (base)
			rd->value = readl(base + rd->offset);
		else if (regmap)
			regmap_read(regmap, rd->offset, &rd->value);
	}
}

void samsung_clk_restore(void __iomem *base,
				      struct regmap *regmap,
				      const struct samsung_clk_reg_dump *rd,
				      unsigned int num_regs)
{
	for (; num_regs > 0; --num_regs, ++rd) {
		if (base)
			writel(rd->value, base + rd->offset);
		else if (regmap)
			regmap_write(regmap, rd->offset, rd->value);
	}
}

struct samsung_clk_reg_dump *samsung_clk_alloc_reg_dump(
						const unsigned long *rdump,
						unsigned long nr_rdump)
{
	struct samsung_clk_reg_dump *rd;
	unsigned int i;

	rd = kzalloc_objs(*rd, nr_rdump);
	if (!rd)
		return NULL;

	for (i = 0; i < nr_rdump; ++i)
		rd[i].offset = rdump[i];

	return rd;
}

/**
 * samsung_clk_init() - Create and initialize a clock provider object
 * @dev:	CMU device to enable runtime PM, or NULL if RPM is not needed
 * @base:	Start address (mapped) of CMU registers
 * @nr_clks:	Total clock count to allocate in clock provider object
 *
 * Setup the essentials required to support clock lookup using Common Clock
 * Framework.
 *
 * Return: Allocated and initialized clock provider object.
 */
struct samsung_clk_provider * __init samsung_clk_init(struct device *dev,
			void __iomem *base, unsigned long nr_clks)
{
	struct samsung_clk_provider *ctx;
	int i;

	ctx = kzalloc_flex(*ctx, clk_data.hws, nr_clks);
	if (!ctx)
		panic("could not allocate clock provider context.\n");

	ctx->clk_data.num = nr_clks;
	for (i = 0; i < nr_clks; ++i)
		ctx->clk_data.hws[i] = ERR_PTR(-ENOENT);

	ctx->dev = dev;
	ctx->reg_base = base;
	spin_lock_init(&ctx->lock);

	return ctx;
}

void __init samsung_clk_of_add_provider(struct device_node *np,
				struct samsung_clk_provider *ctx)
{
	if (np) {
		if (of_clk_add_hw_provider(np, of_clk_hw_onecell_get,
					&ctx->clk_data))
			panic("could not register clk provider\n");
	}
}

/* add a clock instance to the clock lookup table used for dt based lookup */
void samsung_clk_add_lookup(struct samsung_clk_provider *ctx,
			    struct clk_hw *clk_hw, unsigned int id)
{
	if (id)
		ctx->clk_data.hws[id] = clk_hw;
}

/* register a list of aliases */
void __init samsung_clk_register_alias(struct samsung_clk_provider *ctx,
				const struct samsung_clock_alias *list,
				unsigned int nr_clk)
{
	struct clk_hw *clk_hw;
	unsigned int idx, ret;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		if (!list->id) {
			pr_err("%s: clock id missing for index %d\n", __func__,
				idx);
			continue;
		}

		clk_hw = ctx->clk_data.hws[list->id];
		if (!clk_hw) {
			pr_err("%s: failed to find clock %d\n", __func__,
				list->id);
			continue;
		}

		ret = clk_hw_register_clkdev(clk_hw, list->alias,
					     list->dev_name);
		if (ret)
			pr_err("%s: failed to register lookup %s\n",
					__func__, list->alias);
	}
}

/* register a list of fixed clocks */
void __init samsung_clk_register_fixed_rate(struct samsung_clk_provider *ctx,
		const struct samsung_fixed_rate_clock *list,
		unsigned int nr_clk)
{
	struct clk_hw *clk_hw;
	unsigned int idx;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		clk_hw = clk_hw_register_fixed_rate(ctx->dev, list->name,
			list->parent_name, list->flags, list->fixed_rate);
		if (IS_ERR(clk_hw)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		samsung_clk_add_lookup(ctx, clk_hw, list->id);
	}
}

/* register a list of fixed factor clocks */
void __init samsung_clk_register_fixed_factor(struct samsung_clk_provider *ctx,
		const struct samsung_fixed_factor_clock *list, unsigned int nr_clk)
{
	struct clk_hw *clk_hw;
	unsigned int idx;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		clk_hw = clk_hw_register_fixed_factor(ctx->dev, list->name,
			list->parent_name, list->flags, list->mult, list->div);
		if (IS_ERR(clk_hw)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		samsung_clk_add_lookup(ctx, clk_hw, list->id);
	}
}

/* register a list of mux clocks */
void __init samsung_clk_register_mux(struct samsung_clk_provider *ctx,
				const struct samsung_mux_clock *list,
				unsigned int nr_clk)
{
	struct clk_hw *clk_hw;
	unsigned int idx;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		clk_hw = clk_hw_register_mux(ctx->dev, list->name,
			list->parent_names, list->num_parents, list->flags,
			ctx->reg_base + list->offset,
			list->shift, list->width, list->mux_flags, &ctx->lock);
		if (IS_ERR(clk_hw)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		samsung_clk_add_lookup(ctx, clk_hw, list->id);
	}
}

/* register a list of div clocks */
void __init samsung_clk_register_div(struct samsung_clk_provider *ctx,
				const struct samsung_div_clock *list,
				unsigned int nr_clk)
{
	struct clk_hw *clk_hw;
	unsigned int idx;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		if (list->table)
			clk_hw = clk_hw_register_divider_table(ctx->dev,
				list->name, list->parent_name, list->flags,
				ctx->reg_base + list->offset,
				list->shift, list->width, list->div_flags,
				list->table, &ctx->lock);
		else
			clk_hw = clk_hw_register_divider(ctx->dev, list->name,
				list->parent_name, list->flags,
				ctx->reg_base + list->offset, list->shift,
				list->width, list->div_flags, &ctx->lock);
		if (IS_ERR(clk_hw)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		samsung_clk_add_lookup(ctx, clk_hw, list->id);
	}
}

/*
 * Some older DT's have an incorrect CMU resource size which is incompatible
 * with the auto clock mode feature. In such cases we switch back to manual
 * clock gating mode.
 */
bool samsung_is_auto_capable(struct device_node *np)
{
	struct resource res;
	resource_size_t size;

	if (of_address_to_resource(np, 0, &res))
		return false;

	size = resource_size(&res);
	if (size != 0x10000) {
		pr_warn("%pOF: incorrect res size for automatic clocks\n", np);
		return false;
	}
	return true;
}

#define ACG_MSK GENMASK(6, 4)
#define CLK_IDLE GENMASK(5, 4)
static int samsung_auto_clk_gate_is_en(struct clk_hw *hw)
{
	u32 reg;
	struct clk_gate *gate = to_clk_gate(hw);

	reg = readl(gate->reg);
	return ((reg & ACG_MSK) == CLK_IDLE) ? 0 : 1;
}

/* enable and disable are nops in automatic clock mode */
static int samsung_auto_clk_gate_en(struct clk_hw *hw)
{
	return 0;
}

static void samsung_auto_clk_gate_dis(struct clk_hw *hw)
{
}

static const struct clk_ops samsung_auto_clk_gate_ops = {
	.enable = samsung_auto_clk_gate_en,
	.disable = samsung_auto_clk_gate_dis,
	.is_enabled = samsung_auto_clk_gate_is_en,
};

struct clk_hw *samsung_register_auto_gate(struct device *dev,
		struct device_node *np, const char *name,
		const char *parent_name, const struct clk_hw *parent_hw,
		const struct clk_parent_data *parent_data,
		unsigned long flags,
		void __iomem *reg, u8 bit_idx,
		u8 clk_gate_flags, spinlock_t *lock)
{
	struct clk_gate *gate;
	struct clk_hw *hw;
	struct clk_init_data init = {};
	int ret = -EINVAL;

	/* allocate the gate */
	gate = kzalloc_obj(*gate);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &samsung_auto_clk_gate_ops;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.parent_hws = parent_hw ? &parent_hw : NULL;
	init.parent_data = parent_data;
	if (parent_name || parent_hw || parent_data)
		init.num_parents = 1;
	else
		init.num_parents = 0;

	/* struct clk_gate assignments */
	gate->reg = reg;
	gate->bit_idx = bit_idx;
	gate->flags = clk_gate_flags;
	gate->lock = lock;
	gate->hw.init = &init;

	hw = &gate->hw;
	if (dev || !np)
		ret = clk_hw_register(dev, hw);
	else if (np)
		ret = of_clk_hw_register(np, hw);
	if (ret) {
		kfree(gate);
		hw = ERR_PTR(ret);
	}

	return hw;
}

/* register a list of gate clocks */
void __init samsung_clk_register_gate(struct samsung_clk_provider *ctx,
				const struct samsung_gate_clock *list,
				unsigned int nr_clk)
{
	struct clk_hw *clk_hw;
	unsigned int idx;
	void __iomem *reg_offs;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		reg_offs = ctx->reg_base + list->offset;

		if (ctx->auto_clock_gate && ctx->gate_dbg_offset)
			clk_hw = samsung_register_auto_gate(ctx->dev, NULL,
				list->name, list->parent_name, NULL, NULL,
				list->flags, reg_offs + ctx->gate_dbg_offset,
				list->bit_idx, list->gate_flags, &ctx->lock);
		else
			clk_hw = clk_hw_register_gate(ctx->dev, list->name,
				list->parent_name, list->flags,
				ctx->reg_base + list->offset, list->bit_idx,
				list->gate_flags, &ctx->lock);
		if (IS_ERR(clk_hw)) {
			pr_err("%s: failed to register clock %s: %ld\n", __func__,
				list->name, PTR_ERR(clk_hw));
			continue;
		}

		samsung_clk_add_lookup(ctx, clk_hw, list->id);
	}
}

/*
 * obtain the clock speed of all external fixed clock sources from device
 * tree and register it
 */
void __init samsung_clk_of_register_fixed_ext(struct samsung_clk_provider *ctx,
			struct samsung_fixed_rate_clock *fixed_rate_clk,
			unsigned int nr_fixed_rate_clk,
			const struct of_device_id *clk_matches)
{
	const struct of_device_id *match;
	struct device_node *clk_np;
	u32 freq;

	for_each_matching_node_and_match(clk_np, clk_matches, &match) {
		if (of_property_read_u32(clk_np, "clock-frequency", &freq))
			continue;
		fixed_rate_clk[(unsigned long)match->data].fixed_rate = freq;
	}
	samsung_clk_register_fixed_rate(ctx, fixed_rate_clk, nr_fixed_rate_clk);
}

#ifdef CONFIG_PM_SLEEP
static int samsung_clk_suspend(void *data)
{
	struct samsung_clock_reg_cache *reg_cache;

	list_for_each_entry(reg_cache, &clock_reg_cache_list, node) {
		samsung_clk_save(reg_cache->reg_base, reg_cache->sysreg,
				 reg_cache->rdump, reg_cache->rd_num);
		samsung_clk_restore(reg_cache->reg_base, reg_cache->sysreg,
				    reg_cache->rsuspend,
				    reg_cache->rsuspend_num);
	}
	return 0;
}

static void samsung_clk_resume(void *data)
{
	struct samsung_clock_reg_cache *reg_cache;

	list_for_each_entry(reg_cache, &clock_reg_cache_list, node)
		samsung_clk_restore(reg_cache->reg_base, reg_cache->sysreg,
				    reg_cache->rdump, reg_cache->rd_num);
}

static const struct syscore_ops samsung_clk_syscore_ops = {
	.suspend = samsung_clk_suspend,
	.resume = samsung_clk_resume,
};

static struct syscore samsung_clk_syscore = {
	.ops = &samsung_clk_syscore_ops,
};

void samsung_clk_extended_sleep_init(void __iomem *reg_base,
			struct regmap *sysreg,
			const unsigned long *rdump,
			unsigned long nr_rdump,
			const struct samsung_clk_reg_dump *rsuspend,
			unsigned long nr_rsuspend)
{
	struct samsung_clock_reg_cache *reg_cache;

	reg_cache = kzalloc_obj(struct samsung_clock_reg_cache);
	if (!reg_cache)
		panic("could not allocate register reg_cache.\n");
	reg_cache->rdump = samsung_clk_alloc_reg_dump(rdump, nr_rdump);

	if (!reg_cache->rdump)
		panic("could not allocate register dump storage.\n");

	if (list_empty(&clock_reg_cache_list))
		register_syscore(&samsung_clk_syscore);

	reg_cache->reg_base = reg_base;
	reg_cache->sysreg = sysreg;
	reg_cache->rd_num = nr_rdump;
	reg_cache->rsuspend = rsuspend;
	reg_cache->rsuspend_num = nr_rsuspend;
	list_add_tail(&reg_cache->node, &clock_reg_cache_list);
}
#endif

/**
 * samsung_cmu_register_clocks() - Register all clocks provided in CMU object
 * @ctx: Clock provider object
 * @cmu: CMU object with clocks to register
 * @np:  CMU device tree node
 */
void __init samsung_cmu_register_clocks(struct samsung_clk_provider *ctx,
					const struct samsung_cmu_info *cmu,
					struct device_node *np)
{
	if (cmu->auto_clock_gate && samsung_is_auto_capable(np))
		ctx->auto_clock_gate = cmu->auto_clock_gate;

	ctx->gate_dbg_offset = cmu->gate_dbg_offset;
	ctx->option_offset = cmu->option_offset;
	ctx->drcg_offset = cmu->drcg_offset;
	ctx->memclk_offset = cmu->memclk_offset;

	if (cmu->pll_clks)
		samsung_clk_register_pll(ctx, cmu->pll_clks, cmu->nr_pll_clks);
	if (cmu->mux_clks)
		samsung_clk_register_mux(ctx, cmu->mux_clks, cmu->nr_mux_clks);
	if (cmu->div_clks)
		samsung_clk_register_div(ctx, cmu->div_clks, cmu->nr_div_clks);
	if (cmu->gate_clks)
		samsung_clk_register_gate(ctx, cmu->gate_clks,
					  cmu->nr_gate_clks);
	if (cmu->fixed_clks)
		samsung_clk_register_fixed_rate(ctx, cmu->fixed_clks,
						cmu->nr_fixed_clks);
	if (cmu->fixed_factor_clks)
		samsung_clk_register_fixed_factor(ctx, cmu->fixed_factor_clks,
						  cmu->nr_fixed_factor_clks);
	if (cmu->cpu_clks)
		samsung_clk_register_cpu(ctx, cmu->cpu_clks, cmu->nr_cpu_clks);
}

/* Each bit enable/disables DRCG of a bus component */
#define DRCG_EN_MSK	GENMASK(31, 0)
#define MEMCLK_EN	BIT(0)

/* Enable Dynamic Root Clock Gating (DRCG) of bus components */
void samsung_en_dyn_root_clk_gating(struct device_node *np,
				    struct samsung_clk_provider *ctx,
				    const struct samsung_cmu_info *cmu,
				    bool cmu_has_pm)
{
	if (!ctx->auto_clock_gate)
		return;

	ctx->sysreg = syscon_regmap_lookup_by_phandle(np, "samsung,sysreg");
	if (IS_ERR(ctx->sysreg)) {
		pr_warn("%pOF: Unable to get CMU sysreg\n", np);
		ctx->sysreg = NULL;
	} else {
		/* Enable DRCG for all bus components */
		regmap_write(ctx->sysreg, ctx->drcg_offset, DRCG_EN_MSK);
		/* Enable memclk gate (not present on all sysreg) */
		if (ctx->memclk_offset)
			regmap_write_bits(ctx->sysreg, ctx->memclk_offset,
					  MEMCLK_EN, 0x0);

		if (!cmu_has_pm)
			/*
			 * When a CMU has PM support, clocks are saved/restored
			 * via its PM handlers, so only register them with the
			 * syscore suspend / resume paths if PM is not in use.
			 */
			samsung_clk_extended_sleep_init(NULL, ctx->sysreg,
							cmu->sysreg_clk_regs,
							cmu->nr_sysreg_clk_regs,
							NULL, 0);
	}
}

/*
 * Common function which registers plls, muxes, dividers and gates
 * for each CMU. It also add CMU register list to register cache.
 */
struct samsung_clk_provider * __init samsung_cmu_register_one(
			struct device_node *np,
			const struct samsung_cmu_info *cmu)
{
	void __iomem *reg_base;
	struct samsung_clk_provider *ctx;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		panic("%s: failed to map registers\n", __func__);
		return NULL;
	}

	ctx = samsung_clk_init(NULL, reg_base, cmu->nr_clk_ids);
	samsung_cmu_register_clocks(ctx, cmu, np);

	if (cmu->clk_regs)
		samsung_clk_extended_sleep_init(reg_base, NULL,
			cmu->clk_regs, cmu->nr_clk_regs,
			cmu->suspend_regs, cmu->nr_suspend_regs);

	samsung_clk_of_add_provider(np, ctx);

	/* sysreg DT nodes reference a clock in this CMU */
	samsung_en_dyn_root_clk_gating(np, ctx, cmu, false);

	return ctx;
}
