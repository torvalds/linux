/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>

#include "clkc.h"

static DEFINE_SPINLOCK(clk_lock);

static struct clk **clks;
static struct clk_onecell_data clk_data;

struct clk ** __init meson_clk_init(struct device_node *np,
				   unsigned long nr_clks)
{
	clks = kcalloc(nr_clks, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		return ERR_PTR(-ENOMEM);

	clk_data.clks = clks;
	clk_data.clk_num = nr_clks;
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	return clks;
}

static void meson_clk_add_lookup(struct clk *clk, unsigned int id)
{
	if (clks && id)
		clks[id] = clk;
}

static struct clk * __init
meson_clk_register_composite(const struct clk_conf *clk_conf,
			     void __iomem *clk_base)
{
	struct clk *clk;
	struct clk_mux *mux = NULL;
	struct clk_divider *div = NULL;
	struct clk_gate *gate = NULL;
	const struct clk_ops *mux_ops = NULL;
	const struct composite_conf *composite_conf;

	composite_conf = clk_conf->conf.composite;

	if (clk_conf->num_parents > 1) {
		mux = kzalloc(sizeof(*mux), GFP_KERNEL);
		if (!mux)
			return ERR_PTR(-ENOMEM);

		mux->reg = clk_base + clk_conf->reg_off
				+ composite_conf->mux_parm.reg_off;
		mux->shift = composite_conf->mux_parm.shift;
		mux->mask = BIT(composite_conf->mux_parm.width) - 1;
		mux->flags = composite_conf->mux_flags;
		mux->lock = &clk_lock;
		mux->table = composite_conf->mux_table;
		mux_ops = (composite_conf->mux_flags & CLK_MUX_READ_ONLY) ?
			  &clk_mux_ro_ops : &clk_mux_ops;
	}

	if (MESON_PARM_APPLICABLE(&composite_conf->div_parm)) {
		div = kzalloc(sizeof(*div), GFP_KERNEL);
		if (!div) {
			clk = ERR_PTR(-ENOMEM);
			goto error;
		}

		div->reg = clk_base + clk_conf->reg_off
				+ composite_conf->div_parm.reg_off;
		div->shift = composite_conf->div_parm.shift;
		div->width = composite_conf->div_parm.width;
		div->lock = &clk_lock;
		div->flags = composite_conf->div_flags;
		div->table = composite_conf->div_table;
	}

	if (MESON_PARM_APPLICABLE(&composite_conf->gate_parm)) {
		gate = kzalloc(sizeof(*gate), GFP_KERNEL);
		if (!gate) {
			clk = ERR_PTR(-ENOMEM);
			goto error;
		}

		gate->reg = clk_base + clk_conf->reg_off
				+ composite_conf->div_parm.reg_off;
		gate->bit_idx = composite_conf->gate_parm.shift;
		gate->flags = composite_conf->gate_flags;
		gate->lock = &clk_lock;
	}

	clk = clk_register_composite(NULL, clk_conf->clk_name,
				    clk_conf->clks_parent,
				    clk_conf->num_parents,
				    mux ? &mux->hw : NULL, mux_ops,
				    div ? &div->hw : NULL, &clk_divider_ops,
				    gate ? &gate->hw : NULL, &clk_gate_ops,
				    clk_conf->flags);
	if (IS_ERR(clk))
		goto error;

	return clk;

error:
	kfree(gate);
	kfree(div);
	kfree(mux);

	return clk;
}

static struct clk * __init
meson_clk_register_fixed_factor(const struct clk_conf *clk_conf,
				void __iomem *clk_base)
{
	struct clk *clk;
	const struct fixed_fact_conf *fixed_fact_conf;
	const struct parm *p;
	unsigned int mult, div;
	u32 reg;

	fixed_fact_conf = &clk_conf->conf.fixed_fact;

	mult = clk_conf->conf.fixed_fact.mult;
	div = clk_conf->conf.fixed_fact.div;

	if (!mult) {
		mult = 1;
		p = &fixed_fact_conf->mult_parm;
		if (MESON_PARM_APPLICABLE(p)) {
			reg = readl(clk_base + clk_conf->reg_off + p->reg_off);
			mult = PARM_GET(p->width, p->shift, reg);
		}
	}

	if (!div) {
		div = 1;
		p = &fixed_fact_conf->div_parm;
		if (MESON_PARM_APPLICABLE(p)) {
			reg = readl(clk_base + clk_conf->reg_off + p->reg_off);
			mult = PARM_GET(p->width, p->shift, reg);
		}
	}

	clk = clk_register_fixed_factor(NULL,
			clk_conf->clk_name,
			clk_conf->clks_parent[0],
			clk_conf->flags,
			mult, div);

	return clk;
}

static struct clk * __init
meson_clk_register_fixed_rate(const struct clk_conf *clk_conf,
			      void __iomem *clk_base)
{
	struct clk *clk;
	const struct fixed_rate_conf *fixed_rate_conf;
	const struct parm *r;
	unsigned long rate;
	u32 reg;

	fixed_rate_conf = &clk_conf->conf.fixed_rate;
	rate = fixed_rate_conf->rate;

	if (!rate) {
		r = &fixed_rate_conf->rate_parm;
		reg = readl(clk_base + clk_conf->reg_off + r->reg_off);
		rate = PARM_GET(r->width, r->shift, reg);
	}

	rate *= 1000000;

	clk = clk_register_fixed_rate(NULL,
			clk_conf->clk_name,
			clk_conf->num_parents
				? clk_conf->clks_parent[0] : NULL,
			clk_conf->flags, rate);

	return clk;
}

void __init meson_clk_register_clks(const struct clk_conf *clk_confs,
				    unsigned int nr_confs,
				    void __iomem *clk_base)
{
	unsigned int i;
	struct clk *clk = NULL;

	for (i = 0; i < nr_confs; i++) {
		const struct clk_conf *clk_conf = &clk_confs[i];

		switch (clk_conf->clk_type) {
		case CLK_FIXED_RATE:
			clk = meson_clk_register_fixed_rate(clk_conf,
							    clk_base);
			break;
		case CLK_FIXED_FACTOR:
			clk = meson_clk_register_fixed_factor(clk_conf,
							      clk_base);
			break;
		case CLK_COMPOSITE:
			clk = meson_clk_register_composite(clk_conf,
							   clk_base);
			break;
		case CLK_CPU:
			clk = meson_clk_register_cpu(clk_conf, clk_base,
						     &clk_lock);
			break;
		case CLK_PLL:
			clk = meson_clk_register_pll(clk_conf, clk_base,
						     &clk_lock);
			break;
		default:
			clk = NULL;
		}

		if (!clk) {
			pr_err("%s: unknown clock type %d\n", __func__,
			       clk_conf->clk_type);
			continue;
		}

		if (IS_ERR(clk)) {
			pr_warn("%s: Unable to create %s clock\n", __func__,
				clk_conf->clk_name);
			continue;
		}

		meson_clk_add_lookup(clk, clk_conf->clk_id);
	}
}
