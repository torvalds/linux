/*
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * General Purpose Timer Synthesizer clock implementation
 */

#define pr_fmt(fmt) "clk-gpt-synth: " fmt

#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include "clk.h"

#define GPT_MSCALE_MASK		0xFFF
#define GPT_NSCALE_SHIFT	12
#define GPT_NSCALE_MASK		0xF

/*
 * DOC: General Purpose Timer Synthesizer clock
 *
 * Calculates gpt synth clk rate for different values of mscale and nscale
 *
 * Fout= Fin/((2 ^ (N+1)) * (M+1))
 */

#define to_clk_gpt(_hw) container_of(_hw, struct clk_gpt, hw)

static unsigned long gpt_calc_rate(struct clk_hw *hw, unsigned long prate,
		int index)
{
	struct clk_gpt *gpt = to_clk_gpt(hw);
	struct gpt_rate_tbl *rtbl = gpt->rtbl;

	prate /= ((1 << (rtbl[index].nscale + 1)) * (rtbl[index].mscale + 1));

	return prate;
}

static long clk_gpt_round_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long *prate)
{
	struct clk_gpt *gpt = to_clk_gpt(hw);
	int unused;

	return clk_round_rate_index(hw, drate, *prate, gpt_calc_rate,
			gpt->rtbl_cnt, &unused);
}

static unsigned long clk_gpt_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_gpt *gpt = to_clk_gpt(hw);
	unsigned long flags = 0;
	unsigned int div = 1, val;

	if (gpt->lock)
		spin_lock_irqsave(gpt->lock, flags);

	val = readl_relaxed(gpt->reg);

	if (gpt->lock)
		spin_unlock_irqrestore(gpt->lock, flags);

	div += val & GPT_MSCALE_MASK;
	div *= 1 << (((val >> GPT_NSCALE_SHIFT) & GPT_NSCALE_MASK) + 1);

	if (!div)
		return 0;

	return parent_rate / div;
}

/* Configures new clock rate of gpt */
static int clk_gpt_set_rate(struct clk_hw *hw, unsigned long drate,
				unsigned long prate)
{
	struct clk_gpt *gpt = to_clk_gpt(hw);
	struct gpt_rate_tbl *rtbl = gpt->rtbl;
	unsigned long flags = 0, val;
	int i;

	clk_round_rate_index(hw, drate, prate, gpt_calc_rate, gpt->rtbl_cnt,
			&i);

	if (gpt->lock)
		spin_lock_irqsave(gpt->lock, flags);

	val = readl(gpt->reg) & ~GPT_MSCALE_MASK;
	val &= ~(GPT_NSCALE_MASK << GPT_NSCALE_SHIFT);

	val |= rtbl[i].mscale & GPT_MSCALE_MASK;
	val |= (rtbl[i].nscale & GPT_NSCALE_MASK) << GPT_NSCALE_SHIFT;

	writel_relaxed(val, gpt->reg);

	if (gpt->lock)
		spin_unlock_irqrestore(gpt->lock, flags);

	return 0;
}

static struct clk_ops clk_gpt_ops = {
	.recalc_rate = clk_gpt_recalc_rate,
	.round_rate = clk_gpt_round_rate,
	.set_rate = clk_gpt_set_rate,
};

struct clk *clk_register_gpt(const char *name, const char *parent_name, unsigned
		long flags, void __iomem *reg, struct gpt_rate_tbl *rtbl, u8
		rtbl_cnt, spinlock_t *lock)
{
	struct clk_init_data init;
	struct clk_gpt *gpt;
	struct clk *clk;

	if (!name || !parent_name || !reg || !rtbl || !rtbl_cnt) {
		pr_err("Invalid arguments passed");
		return ERR_PTR(-EINVAL);
	}

	gpt = kzalloc(sizeof(*gpt), GFP_KERNEL);
	if (!gpt) {
		pr_err("could not allocate gpt clk\n");
		return ERR_PTR(-ENOMEM);
	}

	/* struct clk_gpt assignments */
	gpt->reg = reg;
	gpt->rtbl = rtbl;
	gpt->rtbl_cnt = rtbl_cnt;
	gpt->lock = lock;
	gpt->hw.init = &init;

	init.name = name;
	init.ops = &clk_gpt_ops;
	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clk = clk_register(NULL, &gpt->hw);
	if (!IS_ERR_OR_NULL(clk))
		return clk;

	pr_err("clk register failed\n");
	kfree(gpt);

	return NULL;
}
