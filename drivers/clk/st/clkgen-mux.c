/*
 * clkgen-mux.c: ST GEN-MUX Clock driver
 *
 * Copyright (C) 2014 STMicroelectronics (R&D) Limited
 *
 * Authors: Stephen Gallimore <stephen.gallimore@st.com>
 *	    Pankaj Dev <pankaj.dev@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/clk-provider.h>

static DEFINE_SPINLOCK(clkgena_divmux_lock);
static DEFINE_SPINLOCK(clkgenf_lock);

static const char ** __init clkgen_mux_get_parents(struct device_node *np,
						       int *num_parents)
{
	const char **parents;
	int nparents, i;

	nparents = of_count_phandle_with_args(np, "clocks", "#clock-cells");
	if (WARN_ON(nparents <= 0))
		return ERR_PTR(-EINVAL);

	parents = kzalloc(nparents * sizeof(const char *), GFP_KERNEL);
	if (!parents)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < nparents; i++)
		parents[i] = of_clk_get_parent_name(np, i);

	*num_parents = nparents;
	return parents;
}

/**
 * DOC: Clock mux with a programmable divider on each of its three inputs.
 *      The mux has an input setting which effectively gates its output.
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional & control gating
 * rate - set rate is supported
 * parent - set/get parent
 */

#define NUM_INPUTS 3

struct clkgena_divmux {
	struct clk_hw hw;
	/* Subclassed mux and divider structures */
	struct clk_mux mux;
	struct clk_divider div[NUM_INPUTS];
	/* Enable/running feedback register bits for each input */
	void __iomem *feedback_reg[NUM_INPUTS];
	int feedback_bit_idx;

	u8              muxsel;
};

#define to_clkgena_divmux(_hw) container_of(_hw, struct clkgena_divmux, hw)

struct clkgena_divmux_data {
	int num_outputs;
	int mux_offset;
	int mux_offset2;
	int mux_start_bit;
	int div_offsets[NUM_INPUTS];
	int fb_offsets[NUM_INPUTS];
	int fb_start_bit_idx;
};

#define CKGAX_CLKOPSRC_SWITCH_OFF 0x3

static int clkgena_divmux_is_running(struct clkgena_divmux *mux)
{
	u32 regval = readl(mux->feedback_reg[mux->muxsel]);
	u32 running = regval & BIT(mux->feedback_bit_idx);
	return !!running;
}

static int clkgena_divmux_enable(struct clk_hw *hw)
{
	struct clkgena_divmux *genamux = to_clkgena_divmux(hw);
	struct clk_hw *mux_hw = &genamux->mux.hw;
	unsigned long timeout;
	int ret = 0;

	mux_hw->clk = hw->clk;

	ret = clk_mux_ops.set_parent(mux_hw, genamux->muxsel);
	if (ret)
		return ret;

	timeout = jiffies + msecs_to_jiffies(10);

	while (!clkgena_divmux_is_running(genamux)) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		cpu_relax();
	}

	return 0;
}

static void clkgena_divmux_disable(struct clk_hw *hw)
{
	struct clkgena_divmux *genamux = to_clkgena_divmux(hw);
	struct clk_hw *mux_hw = &genamux->mux.hw;

	mux_hw->clk = hw->clk;

	clk_mux_ops.set_parent(mux_hw, CKGAX_CLKOPSRC_SWITCH_OFF);
}

static int clkgena_divmux_is_enabled(struct clk_hw *hw)
{
	struct clkgena_divmux *genamux = to_clkgena_divmux(hw);
	struct clk_hw *mux_hw = &genamux->mux.hw;

	mux_hw->clk = hw->clk;

	return (s8)clk_mux_ops.get_parent(mux_hw) > 0;
}

u8 clkgena_divmux_get_parent(struct clk_hw *hw)
{
	struct clkgena_divmux *genamux = to_clkgena_divmux(hw);
	struct clk_hw *mux_hw = &genamux->mux.hw;

	mux_hw->clk = hw->clk;

	genamux->muxsel = clk_mux_ops.get_parent(mux_hw);
	if ((s8)genamux->muxsel < 0) {
		pr_debug("%s: %s: Invalid parent, setting to default.\n",
		      __func__, __clk_get_name(hw->clk));
		genamux->muxsel = 0;
	}

	return genamux->muxsel;
}

static int clkgena_divmux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clkgena_divmux *genamux = to_clkgena_divmux(hw);

	if (index >= CKGAX_CLKOPSRC_SWITCH_OFF)
		return -EINVAL;

	genamux->muxsel = index;

	/*
	 * If the mux is already enabled, call enable directly to set the
	 * new mux position and wait for it to start running again. Otherwise
	 * do nothing.
	 */
	if (clkgena_divmux_is_enabled(hw))
		clkgena_divmux_enable(hw);

	return 0;
}

unsigned long clkgena_divmux_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clkgena_divmux *genamux = to_clkgena_divmux(hw);
	struct clk_hw *div_hw = &genamux->div[genamux->muxsel].hw;

	div_hw->clk = hw->clk;

	return clk_divider_ops.recalc_rate(div_hw, parent_rate);
}

static int clkgena_divmux_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clkgena_divmux *genamux = to_clkgena_divmux(hw);
	struct clk_hw *div_hw = &genamux->div[genamux->muxsel].hw;

	div_hw->clk = hw->clk;

	return clk_divider_ops.set_rate(div_hw, rate, parent_rate);
}

static long clkgena_divmux_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *prate)
{
	struct clkgena_divmux *genamux = to_clkgena_divmux(hw);
	struct clk_hw *div_hw = &genamux->div[genamux->muxsel].hw;

	div_hw->clk = hw->clk;

	return clk_divider_ops.round_rate(div_hw, rate, prate);
}

static const struct clk_ops clkgena_divmux_ops = {
	.enable = clkgena_divmux_enable,
	.disable = clkgena_divmux_disable,
	.is_enabled = clkgena_divmux_is_enabled,
	.get_parent = clkgena_divmux_get_parent,
	.set_parent = clkgena_divmux_set_parent,
	.round_rate = clkgena_divmux_round_rate,
	.recalc_rate = clkgena_divmux_recalc_rate,
	.set_rate = clkgena_divmux_set_rate,
};

/**
 * clk_register_genamux - register a genamux clock with the clock framework
 */
struct clk *clk_register_genamux(const char *name,
				const char **parent_names, u8 num_parents,
				void __iomem *reg,
				const struct clkgena_divmux_data *muxdata,
				u32 idx)
{
	/*
	 * Fixed constants across all ClockgenA variants
	 */
	const int mux_width = 2;
	const int divider_width = 5;
	struct clkgena_divmux *genamux;
	struct clk *clk;
	struct clk_init_data init;
	int i;

	genamux = kzalloc(sizeof(*genamux), GFP_KERNEL);
	if (!genamux)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clkgena_divmux_ops;
	init.flags = CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	genamux->mux.lock  = &clkgena_divmux_lock;
	genamux->mux.mask = BIT(mux_width) - 1;
	genamux->mux.shift = muxdata->mux_start_bit + (idx * mux_width);
	if (genamux->mux.shift > 31) {
		/*
		 * We have spilled into the second mux register so
		 * adjust the register address and the bit shift accordingly
		 */
		genamux->mux.reg = reg + muxdata->mux_offset2;
		genamux->mux.shift -= 32;
	} else {
		genamux->mux.reg   = reg + muxdata->mux_offset;
	}

	for (i = 0; i < NUM_INPUTS; i++) {
		/*
		 * Divider config for each input
		 */
		void __iomem *divbase = reg + muxdata->div_offsets[i];
		genamux->div[i].width = divider_width;
		genamux->div[i].reg = divbase + (idx * sizeof(u32));

		/*
		 * Mux enabled/running feedback register for each input.
		 */
		genamux->feedback_reg[i] = reg + muxdata->fb_offsets[i];
	}

	genamux->feedback_bit_idx = muxdata->fb_start_bit_idx + idx;
	genamux->hw.init = &init;

	clk = clk_register(NULL, &genamux->hw);
	if (IS_ERR(clk)) {
		kfree(genamux);
		goto err;
	}

	pr_debug("%s: parent %s rate %lu\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			clk_get_rate(clk));
err:
	return clk;
}

static struct clkgena_divmux_data st_divmux_c65hs = {
	.num_outputs = 4,
	.mux_offset = 0x14,
	.mux_start_bit = 0,
	.div_offsets = { 0x800, 0x900, 0xb00 },
	.fb_offsets = { 0x18, 0x1c, 0x20 },
	.fb_start_bit_idx = 0,
};

static struct clkgena_divmux_data st_divmux_c65ls = {
	.num_outputs = 14,
	.mux_offset = 0x14,
	.mux_offset2 = 0x24,
	.mux_start_bit = 8,
	.div_offsets = { 0x810, 0xa10, 0xb10 },
	.fb_offsets = { 0x18, 0x1c, 0x20 },
	.fb_start_bit_idx = 4,
};

static struct clkgena_divmux_data st_divmux_c32odf0 = {
	.num_outputs = 8,
	.mux_offset = 0x1c,
	.mux_start_bit = 0,
	.div_offsets = { 0x800, 0x900, 0xa60 },
	.fb_offsets = { 0x2c, 0x24, 0x28 },
	.fb_start_bit_idx = 0,
};

static struct clkgena_divmux_data st_divmux_c32odf1 = {
	.num_outputs = 8,
	.mux_offset = 0x1c,
	.mux_start_bit = 16,
	.div_offsets = { 0x820, 0x980, 0xa80 },
	.fb_offsets = { 0x2c, 0x24, 0x28 },
	.fb_start_bit_idx = 8,
};

static struct clkgena_divmux_data st_divmux_c32odf2 = {
	.num_outputs = 8,
	.mux_offset = 0x20,
	.mux_start_bit = 0,
	.div_offsets = { 0x840, 0xa20, 0xb10 },
	.fb_offsets = { 0x2c, 0x24, 0x28 },
	.fb_start_bit_idx = 16,
};

static struct clkgena_divmux_data st_divmux_c32odf3 = {
	.num_outputs = 8,
	.mux_offset = 0x20,
	.mux_start_bit = 16,
	.div_offsets = { 0x860, 0xa40, 0xb30 },
	.fb_offsets = { 0x2c, 0x24, 0x28 },
	.fb_start_bit_idx = 24,
};

static struct of_device_id clkgena_divmux_of_match[] = {
	{
		.compatible = "st,clkgena-divmux-c65-hs",
		.data = &st_divmux_c65hs,
	},
	{
		.compatible = "st,clkgena-divmux-c65-ls",
		.data = &st_divmux_c65ls,
	},
	{
		.compatible = "st,clkgena-divmux-c32-odf0",
		.data = &st_divmux_c32odf0,
	},
	{
		.compatible = "st,clkgena-divmux-c32-odf1",
		.data = &st_divmux_c32odf1,
	},
	{
		.compatible = "st,clkgena-divmux-c32-odf2",
		.data = &st_divmux_c32odf2,
	},
	{
		.compatible = "st,clkgena-divmux-c32-odf3",
		.data = &st_divmux_c32odf3,
	},
	{}
};

static void __iomem * __init clkgen_get_register_base(
				struct device_node *np)
{
	struct device_node *pnode;
	void __iomem *reg = NULL;

	pnode = of_get_parent(np);
	if (!pnode)
		return NULL;

	reg = of_iomap(pnode, 0);

	of_node_put(pnode);
	return reg;
}

void __init st_of_clkgena_divmux_setup(struct device_node *np)
{
	const struct of_device_id *match;
	const struct clkgena_divmux_data *data;
	struct clk_onecell_data *clk_data;
	void __iomem *reg;
	const char **parents;
	int num_parents = 0, i;

	match = of_match_node(clkgena_divmux_of_match, np);
	if (WARN_ON(!match))
		return;

	data = (struct clkgena_divmux_data *)match->data;

	reg = clkgen_get_register_base(np);
	if (!reg)
		return;

	parents = clkgen_mux_get_parents(np, &num_parents);
	if (IS_ERR(parents))
		return;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		goto err;

	clk_data->clk_num = data->num_outputs;
	clk_data->clks = kzalloc(clk_data->clk_num * sizeof(struct clk *),
				 GFP_KERNEL);

	if (!clk_data->clks)
		goto err;

	for (i = 0; i < clk_data->clk_num; i++) {
		struct clk *clk;
		const char *clk_name;

		if (of_property_read_string_index(np, "clock-output-names",
						  i, &clk_name))
			break;

		/*
		 * If we read an empty clock name then the output is unused
		 */
		if (*clk_name == '\0')
			continue;

		clk = clk_register_genamux(clk_name, parents, num_parents,
					   reg, data, i);

		if (IS_ERR(clk))
			goto err;

		clk_data->clks[i] = clk;
	}

	kfree(parents);

	of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);
	return;
err:
	if (clk_data)
		kfree(clk_data->clks);

	kfree(clk_data);
	kfree(parents);
}
CLK_OF_DECLARE(clkgenadivmux, "st,clkgena-divmux", st_of_clkgena_divmux_setup);

struct clkgena_prediv_data {
	u32 offset;
	u8 shift;
	struct clk_div_table *table;
};

static struct clk_div_table prediv_table16[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 16 },
	{ .div = 0 },
};

static struct clkgena_prediv_data prediv_c65_data = {
	.offset = 0x4c,
	.shift = 31,
	.table = prediv_table16,
};

static struct clkgena_prediv_data prediv_c32_data = {
	.offset = 0x50,
	.shift = 1,
	.table = prediv_table16,
};

static struct of_device_id clkgena_prediv_of_match[] = {
	{ .compatible = "st,clkgena-prediv-c65", .data = &prediv_c65_data },
	{ .compatible = "st,clkgena-prediv-c32", .data = &prediv_c32_data },
	{}
};

void __init st_of_clkgena_prediv_setup(struct device_node *np)
{
	const struct of_device_id *match;
	void __iomem *reg;
	const char *parent_name, *clk_name;
	struct clk *clk;
	struct clkgena_prediv_data *data;

	match = of_match_node(clkgena_prediv_of_match, np);
	if (!match) {
		pr_err("%s: No matching data\n", __func__);
		return;
	}

	data = (struct clkgena_prediv_data *)match->data;

	reg = clkgen_get_register_base(np);
	if (!reg)
		return;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name)
		return;

	if (of_property_read_string_index(np, "clock-output-names",
					  0, &clk_name))
		return;

	clk = clk_register_divider_table(NULL, clk_name, parent_name, 0,
					 reg + data->offset, data->shift, 1,
					 0, data->table, NULL);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
	pr_debug("%s: parent %s rate %u\n",
		__clk_get_name(clk),
		__clk_get_name(clk_get_parent(clk)),
		(unsigned int)clk_get_rate(clk));

	return;
}
CLK_OF_DECLARE(clkgenaprediv, "st,clkgena-prediv", st_of_clkgena_prediv_setup);

struct clkgen_mux_data {
	u32 offset;
	u8 shift;
	u8 width;
	spinlock_t *lock;
	unsigned long clk_flags;
	u8 mux_flags;
};

static struct clkgen_mux_data clkgen_mux_c_vcc_hd_416 = {
	.offset = 0,
	.shift = 0,
	.width = 1,
};

static struct clkgen_mux_data clkgen_mux_f_vcc_fvdp_416 = {
	.offset = 0,
	.shift = 0,
	.width = 1,
};

static struct clkgen_mux_data clkgen_mux_f_vcc_hva_416 = {
	.offset = 0,
	.shift = 0,
	.width = 1,
};

static struct clkgen_mux_data clkgen_mux_f_vcc_hd_416 = {
	.offset = 0,
	.shift = 16,
	.width = 1,
	.lock = &clkgenf_lock,
};

static struct clkgen_mux_data clkgen_mux_c_vcc_sd_416 = {
	.offset = 0,
	.shift = 17,
	.width = 1,
	.lock = &clkgenf_lock,
};

static struct clkgen_mux_data stih415_a9_mux_data = {
	.offset = 0,
	.shift = 1,
	.width = 2,
};
static struct clkgen_mux_data stih416_a9_mux_data = {
	.offset = 0,
	.shift = 0,
	.width = 2,
};

static struct of_device_id mux_of_match[] = {
	{
		.compatible = "st,stih416-clkgenc-vcc-hd",
		.data = &clkgen_mux_c_vcc_hd_416,
	},
	{
		.compatible = "st,stih416-clkgenf-vcc-fvdp",
		.data = &clkgen_mux_f_vcc_fvdp_416,
	},
	{
		.compatible = "st,stih416-clkgenf-vcc-hva",
		.data = &clkgen_mux_f_vcc_hva_416,
	},
	{
		.compatible = "st,stih416-clkgenf-vcc-hd",
		.data = &clkgen_mux_f_vcc_hd_416,
	},
	{
		.compatible = "st,stih416-clkgenf-vcc-sd",
		.data = &clkgen_mux_c_vcc_sd_416,
	},
	{
		.compatible = "st,stih415-clkgen-a9-mux",
		.data = &stih415_a9_mux_data,
	},
	{
		.compatible = "st,stih416-clkgen-a9-mux",
		.data = &stih416_a9_mux_data,
	},
	{}
};

void __init st_of_clkgen_mux_setup(struct device_node *np)
{
	const struct of_device_id *match;
	struct clk *clk;
	void __iomem *reg;
	const char **parents;
	int num_parents;
	struct clkgen_mux_data *data;

	match = of_match_node(mux_of_match, np);
	if (!match) {
		pr_err("%s: No matching data\n", __func__);
		return;
	}

	data = (struct clkgen_mux_data *)match->data;

	reg = of_iomap(np, 0);
	if (!reg) {
		pr_err("%s: Failed to get base address\n", __func__);
		return;
	}

	parents = clkgen_mux_get_parents(np, &num_parents);
	if (IS_ERR(parents)) {
		pr_err("%s: Failed to get parents (%ld)\n",
				__func__, PTR_ERR(parents));
		return;
	}

	clk = clk_register_mux(NULL, np->name, parents, num_parents,
				data->clk_flags | CLK_SET_RATE_PARENT,
				reg + data->offset,
				data->shift, data->width, data->mux_flags,
				data->lock);
	if (IS_ERR(clk))
		goto err;

	pr_debug("%s: parent %s rate %u\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			(unsigned int)clk_get_rate(clk));

	of_clk_add_provider(np, of_clk_src_simple_get, clk);

err:
	kfree(parents);

	return;
}
CLK_OF_DECLARE(clkgen_mux, "st,clkgen-mux", st_of_clkgen_mux_setup);

#define VCC_MAX_CHANNELS 16

#define VCC_GATE_OFFSET 0x0
#define VCC_MUX_OFFSET 0x4
#define VCC_DIV_OFFSET 0x8

struct clkgen_vcc_data {
	spinlock_t *lock;
	unsigned long clk_flags;
};

static struct clkgen_vcc_data st_clkgenc_vcc_416 = {
	.clk_flags = CLK_SET_RATE_PARENT,
};

static struct clkgen_vcc_data st_clkgenf_vcc_416 = {
	.lock = &clkgenf_lock,
};

static struct of_device_id vcc_of_match[] = {
	{ .compatible = "st,stih416-clkgenc", .data = &st_clkgenc_vcc_416 },
	{ .compatible = "st,stih416-clkgenf", .data = &st_clkgenf_vcc_416 },
	{}
};

void __init st_of_clkgen_vcc_setup(struct device_node *np)
{
	const struct of_device_id *match;
	void __iomem *reg;
	const char **parents;
	int num_parents, i;
	struct clk_onecell_data *clk_data;
	struct clkgen_vcc_data *data;

	match = of_match_node(vcc_of_match, np);
	if (WARN_ON(!match))
		return;
	data = (struct clkgen_vcc_data *)match->data;

	reg = of_iomap(np, 0);
	if (!reg)
		return;

	parents = clkgen_mux_get_parents(np, &num_parents);
	if (IS_ERR(parents))
		return;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		goto err;

	clk_data->clk_num = VCC_MAX_CHANNELS;
	clk_data->clks = kzalloc(clk_data->clk_num * sizeof(struct clk *),
				 GFP_KERNEL);

	if (!clk_data->clks)
		goto err;

	for (i = 0; i < clk_data->clk_num; i++) {
		struct clk *clk;
		const char *clk_name;
		struct clk_gate *gate;
		struct clk_divider *div;
		struct clk_mux *mux;

		if (of_property_read_string_index(np, "clock-output-names",
						  i, &clk_name))
			break;

		/*
		 * If we read an empty clock name then the output is unused
		 */
		if (*clk_name == '\0')
			continue;

		gate = kzalloc(sizeof(struct clk_gate), GFP_KERNEL);
		if (!gate)
			break;

		div = kzalloc(sizeof(struct clk_divider), GFP_KERNEL);
		if (!div) {
			kfree(gate);
			break;
		}

		mux = kzalloc(sizeof(struct clk_mux), GFP_KERNEL);
		if (!mux) {
			kfree(gate);
			kfree(div);
			break;
		}

		gate->reg = reg + VCC_GATE_OFFSET;
		gate->bit_idx = i;
		gate->flags = CLK_GATE_SET_TO_DISABLE;
		gate->lock = data->lock;

		div->reg = reg + VCC_DIV_OFFSET;
		div->shift = 2 * i;
		div->width = 2;
		div->flags = CLK_DIVIDER_POWER_OF_TWO;

		mux->reg = reg + VCC_MUX_OFFSET;
		mux->shift = 2 * i;
		mux->mask = 0x3;

		clk = clk_register_composite(NULL, clk_name, parents,
					     num_parents,
					     &mux->hw, &clk_mux_ops,
					     &div->hw, &clk_divider_ops,
					     &gate->hw, &clk_gate_ops,
					     data->clk_flags);
		if (IS_ERR(clk)) {
			kfree(gate);
			kfree(div);
			kfree(mux);
			goto err;
		}

		pr_debug("%s: parent %s rate %u\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			(unsigned int)clk_get_rate(clk));

		clk_data->clks[i] = clk;
	}

	kfree(parents);

	of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);
	return;

err:
	for (i = 0; i < clk_data->clk_num; i++) {
		struct clk_composite *composite;

		if (!clk_data->clks[i])
			continue;

		composite = container_of(__clk_get_hw(clk_data->clks[i]),
					 struct clk_composite, hw);
		kfree(container_of(composite->gate_hw, struct clk_gate, hw));
		kfree(container_of(composite->rate_hw, struct clk_divider, hw));
		kfree(container_of(composite->mux_hw, struct clk_mux, hw));
	}

	if (clk_data)
		kfree(clk_data->clks);

	kfree(clk_data);
	kfree(parents);
}
CLK_OF_DECLARE(clkgen_vcc, "st,clkgen-vcc", st_of_clkgen_vcc_setup);
