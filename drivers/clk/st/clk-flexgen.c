/*
 * clk-flexgen.c
 *
 * Copyright (C) ST-Microelectronics SA 2013
 * Author:  Maxime Coquelin <maxime.coquelin@st.com> for ST-Microelectronics.
 * License terms:  GNU General Public License (GPL), version 2  */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/of_address.h>

struct clkgen_data {
	unsigned long flags;
	bool mode;
};

struct flexgen {
	struct clk_hw hw;

	/* Crossbar */
	struct clk_mux mux;
	/* Pre-divisor's gate */
	struct clk_gate pgate;
	/* Pre-divisor */
	struct clk_divider pdiv;
	/* Final divisor's gate */
	struct clk_gate fgate;
	/* Final divisor */
	struct clk_divider fdiv;
	/* Asynchronous mode control */
	struct clk_gate sync;
	/* hw control flags */
	bool control_mode;
};

#define to_flexgen(_hw) container_of(_hw, struct flexgen, hw)
#define to_clk_gate(_hw) container_of(_hw, struct clk_gate, hw)

static int flexgen_enable(struct clk_hw *hw)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *pgate_hw = &flexgen->pgate.hw;
	struct clk_hw *fgate_hw = &flexgen->fgate.hw;

	__clk_hw_set_clk(pgate_hw, hw);
	__clk_hw_set_clk(fgate_hw, hw);

	clk_gate_ops.enable(pgate_hw);

	clk_gate_ops.enable(fgate_hw);

	pr_debug("%s: flexgen output enabled\n", clk_hw_get_name(hw));
	return 0;
}

static void flexgen_disable(struct clk_hw *hw)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *fgate_hw = &flexgen->fgate.hw;

	/* disable only the final gate */
	__clk_hw_set_clk(fgate_hw, hw);

	clk_gate_ops.disable(fgate_hw);

	pr_debug("%s: flexgen output disabled\n", clk_hw_get_name(hw));
}

static int flexgen_is_enabled(struct clk_hw *hw)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *fgate_hw = &flexgen->fgate.hw;

	__clk_hw_set_clk(fgate_hw, hw);

	if (!clk_gate_ops.is_enabled(fgate_hw))
		return 0;

	return 1;
}

static u8 flexgen_get_parent(struct clk_hw *hw)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *mux_hw = &flexgen->mux.hw;

	__clk_hw_set_clk(mux_hw, hw);

	return clk_mux_ops.get_parent(mux_hw);
}

static int flexgen_set_parent(struct clk_hw *hw, u8 index)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *mux_hw = &flexgen->mux.hw;

	__clk_hw_set_clk(mux_hw, hw);

	return clk_mux_ops.set_parent(mux_hw, index);
}

static inline unsigned long
clk_best_div(unsigned long parent_rate, unsigned long rate)
{
	return parent_rate / rate + ((rate > (2*(parent_rate % rate))) ? 0 : 1);
}

static long flexgen_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *prate)
{
	unsigned long div;

	/* Round div according to exact prate and wished rate */
	div = clk_best_div(*prate, rate);

	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		*prate = rate * div;
		return rate;
	}

	return *prate / div;
}

static unsigned long flexgen_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *pdiv_hw = &flexgen->pdiv.hw;
	struct clk_hw *fdiv_hw = &flexgen->fdiv.hw;
	unsigned long mid_rate;

	__clk_hw_set_clk(pdiv_hw, hw);
	__clk_hw_set_clk(fdiv_hw, hw);

	mid_rate = clk_divider_ops.recalc_rate(pdiv_hw, parent_rate);

	return clk_divider_ops.recalc_rate(fdiv_hw, mid_rate);
}

static int flexgen_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *pdiv_hw = &flexgen->pdiv.hw;
	struct clk_hw *fdiv_hw = &flexgen->fdiv.hw;
	struct clk_hw *sync_hw = &flexgen->sync.hw;
	struct clk_gate *config = to_clk_gate(sync_hw);
	unsigned long div = 0;
	int ret = 0;
	u32 reg;

	__clk_hw_set_clk(pdiv_hw, hw);
	__clk_hw_set_clk(fdiv_hw, hw);

	if (flexgen->control_mode) {
		reg = readl(config->reg);
		reg &= ~BIT(config->bit_idx);
		writel(reg, config->reg);
	}

	div = clk_best_div(parent_rate, rate);

	/*
	* pdiv is mainly targeted for low freq results, while fdiv
	* should be used for div <= 64. The other way round can
	* lead to 'duty cycle' issues.
	*/

	if (div <= 64) {
		clk_divider_ops.set_rate(pdiv_hw, parent_rate, parent_rate);
		ret = clk_divider_ops.set_rate(fdiv_hw, rate, rate * div);
	} else {
		clk_divider_ops.set_rate(fdiv_hw, parent_rate, parent_rate);
		ret = clk_divider_ops.set_rate(pdiv_hw, rate, rate * div);
	}

	return ret;
}

static const struct clk_ops flexgen_ops = {
	.enable = flexgen_enable,
	.disable = flexgen_disable,
	.is_enabled = flexgen_is_enabled,
	.get_parent = flexgen_get_parent,
	.set_parent = flexgen_set_parent,
	.round_rate = flexgen_round_rate,
	.recalc_rate = flexgen_recalc_rate,
	.set_rate = flexgen_set_rate,
};

static struct clk *clk_register_flexgen(const char *name,
				const char **parent_names, u8 num_parents,
				void __iomem *reg, spinlock_t *lock, u32 idx,
				unsigned long flexgen_flags, bool mode) {
	struct flexgen *fgxbar;
	struct clk *clk;
	struct clk_init_data init = {};
	u32  xbar_shift;
	void __iomem *xbar_reg, *fdiv_reg;

	fgxbar = kzalloc(sizeof(struct flexgen), GFP_KERNEL);
	if (!fgxbar)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &flexgen_ops;
	init.flags = CLK_IS_BASIC | CLK_GET_RATE_NOCACHE | flexgen_flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	xbar_reg = reg + 0x18 + (idx & ~0x3);
	xbar_shift = (idx % 4) * 0x8;
	fdiv_reg = reg + 0x164 + idx * 4;

	/* Crossbar element config */
	fgxbar->mux.lock = lock;
	fgxbar->mux.mask = BIT(6) - 1;
	fgxbar->mux.reg = xbar_reg;
	fgxbar->mux.shift = xbar_shift;
	fgxbar->mux.table = NULL;


	/* Pre-divider's gate config (in xbar register)*/
	fgxbar->pgate.lock = lock;
	fgxbar->pgate.reg = xbar_reg;
	fgxbar->pgate.bit_idx = xbar_shift + 6;

	/* Pre-divider config */
	fgxbar->pdiv.lock = lock;
	fgxbar->pdiv.reg = reg + 0x58 + idx * 4;
	fgxbar->pdiv.width = 10;

	/* Final divider's gate config */
	fgxbar->fgate.lock = lock;
	fgxbar->fgate.reg = fdiv_reg;
	fgxbar->fgate.bit_idx = 6;

	/* Final divider config */
	fgxbar->fdiv.lock = lock;
	fgxbar->fdiv.reg = fdiv_reg;
	fgxbar->fdiv.width = 6;

	/* Final divider sync config */
	fgxbar->sync.lock = lock;
	fgxbar->sync.reg = fdiv_reg;
	fgxbar->sync.bit_idx = 7;

	fgxbar->control_mode = mode;

	fgxbar->hw.init = &init;

	clk = clk_register(NULL, &fgxbar->hw);
	if (IS_ERR(clk))
		kfree(fgxbar);
	else
		pr_debug("%s: parent %s rate %u\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			(unsigned int)clk_get_rate(clk));
	return clk;
}

static const char ** __init flexgen_get_parents(struct device_node *np,
						       int *num_parents)
{
	const char **parents;
	unsigned int nparents;

	nparents = of_clk_get_parent_count(np);
	if (WARN_ON(!nparents))
		return NULL;

	parents = kcalloc(nparents, sizeof(const char *), GFP_KERNEL);
	if (!parents)
		return NULL;

	*num_parents = of_clk_parent_fill(np, parents, nparents);

	return parents;
}

static const struct clkgen_data clkgen_audio = {
	.flags = CLK_SET_RATE_PARENT,
};

static const struct clkgen_data clkgen_video = {
	.flags = CLK_SET_RATE_PARENT,
	.mode = 1,
};

static const struct of_device_id flexgen_of_match[] = {
	{
		.compatible = "st,flexgen-audio",
		.data = &clkgen_audio,
	},
	{
		.compatible = "st,flexgen-video",
		.data = &clkgen_video,
	},
	{}
};

static void __init st_of_flexgen_setup(struct device_node *np)
{
	struct device_node *pnode;
	void __iomem *reg;
	struct clk_onecell_data *clk_data;
	const char **parents;
	int num_parents, i;
	spinlock_t *rlock = NULL;
	const struct of_device_id *match;
	struct clkgen_data *data = NULL;
	unsigned long flex_flags = 0;
	int ret;
	bool clk_mode = 0;

	pnode = of_get_parent(np);
	if (!pnode)
		return;

	reg = of_iomap(pnode, 0);
	if (!reg)
		return;

	parents = flexgen_get_parents(np, &num_parents);
	if (!parents) {
		iounmap(reg);
		return;
	}

	match = of_match_node(flexgen_of_match, np);
	if (match) {
		data = (struct clkgen_data *)match->data;
		flex_flags = data->flags;
		clk_mode = data->mode;
	}

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		goto err;

	ret = of_property_count_strings(np, "clock-output-names");
	if (ret <= 0) {
		pr_err("%s: Failed to get number of output clocks (%d)",
				__func__, clk_data->clk_num);
		goto err;
	}
	clk_data->clk_num = ret;

	clk_data->clks = kcalloc(clk_data->clk_num, sizeof(struct clk *),
			GFP_KERNEL);
	if (!clk_data->clks)
		goto err;

	rlock = kzalloc(sizeof(spinlock_t), GFP_KERNEL);
	if (!rlock)
		goto err;

	spin_lock_init(rlock);

	for (i = 0; i < clk_data->clk_num; i++) {
		struct clk *clk;
		const char *clk_name;

		if (of_property_read_string_index(np, "clock-output-names",
						  i, &clk_name)) {
			break;
		}

		of_clk_detect_critical(np, i, &flex_flags);

		/*
		 * If we read an empty clock name then the output is unused
		 */
		if (*clk_name == '\0')
			continue;

		clk = clk_register_flexgen(clk_name, parents, num_parents,
					   reg, rlock, i, flex_flags, clk_mode);

		if (IS_ERR(clk))
			goto err;

		clk_data->clks[i] = clk;
	}

	kfree(parents);
	of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);

	return;

err:
	iounmap(reg);
	if (clk_data)
		kfree(clk_data->clks);
	kfree(clk_data);
	kfree(parents);
	kfree(rlock);
}
CLK_OF_DECLARE(flexgen, "st,flexgen", st_of_flexgen_setup);
