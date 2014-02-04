/*
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/at91_pmc.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include "pmc.h"

#define PROG_SOURCE_MAX		5
#define PROG_ID_MAX		7

#define PROG_STATUS_MASK(id)	(1 << ((id) + 8))
#define PROG_PRES_MASK		0x7
#define PROG_MAX_RM9200_CSS	3

struct clk_programmable_layout {
	u8 pres_shift;
	u8 css_mask;
	u8 have_slck_mck;
};

struct clk_programmable {
	struct clk_hw hw;
	struct at91_pmc *pmc;
	unsigned int irq;
	wait_queue_head_t wait;
	u8 id;
	u8 css;
	u8 pres;
	u8 slckmck;
	const struct clk_programmable_layout *layout;
};

#define to_clk_programmable(hw) container_of(hw, struct clk_programmable, hw)


static irqreturn_t clk_programmable_irq_handler(int irq, void *dev_id)
{
	struct clk_programmable *prog = (struct clk_programmable *)dev_id;

	wake_up(&prog->wait);

	return IRQ_HANDLED;
}

static int clk_programmable_prepare(struct clk_hw *hw)
{
	u32 tmp;
	struct clk_programmable *prog = to_clk_programmable(hw);
	struct at91_pmc *pmc = prog->pmc;
	const struct clk_programmable_layout *layout = prog->layout;
	u8 id = prog->id;
	u32 mask = PROG_STATUS_MASK(id);

	tmp = prog->css | (prog->pres << layout->pres_shift);
	if (layout->have_slck_mck && prog->slckmck)
		tmp |= AT91_PMC_CSSMCK_MCK;

	pmc_write(pmc, AT91_PMC_PCKR(id), tmp);

	while (!(pmc_read(pmc, AT91_PMC_SR) & mask))
		wait_event(prog->wait, pmc_read(pmc, AT91_PMC_SR) & mask);

	return 0;
}

static int clk_programmable_is_ready(struct clk_hw *hw)
{
	struct clk_programmable *prog = to_clk_programmable(hw);
	struct at91_pmc *pmc = prog->pmc;

	return !!(pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_PCKR(prog->id));
}

static unsigned long clk_programmable_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	u32 tmp;
	struct clk_programmable *prog = to_clk_programmable(hw);
	struct at91_pmc *pmc = prog->pmc;
	const struct clk_programmable_layout *layout = prog->layout;

	tmp = pmc_read(pmc, AT91_PMC_PCKR(prog->id));
	prog->pres = (tmp >> layout->pres_shift) & PROG_PRES_MASK;

	return parent_rate >> prog->pres;
}

static long clk_programmable_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	unsigned long best_rate = *parent_rate;
	unsigned long best_diff;
	unsigned long new_diff;
	unsigned long cur_rate;
	int shift = shift;

	if (rate > *parent_rate)
		return *parent_rate;
	else
		best_diff = *parent_rate - rate;

	if (!best_diff)
		return best_rate;

	for (shift = 1; shift < PROG_PRES_MASK; shift++) {
		cur_rate = *parent_rate >> shift;

		if (cur_rate > rate)
			new_diff = cur_rate - rate;
		else
			new_diff = rate - cur_rate;

		if (!new_diff)
			return cur_rate;

		if (new_diff < best_diff) {
			best_diff = new_diff;
			best_rate = cur_rate;
		}

		if (rate > cur_rate)
			break;
	}

	return best_rate;
}

static int clk_programmable_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_programmable *prog = to_clk_programmable(hw);
	const struct clk_programmable_layout *layout = prog->layout;
	if (index > layout->css_mask) {
		if (index > PROG_MAX_RM9200_CSS && layout->have_slck_mck) {
			prog->css = 0;
			prog->slckmck = 1;
			return 0;
		} else {
			return -EINVAL;
		}
	}

	prog->css = index;
	return 0;
}

static u8 clk_programmable_get_parent(struct clk_hw *hw)
{
	u32 tmp;
	u8 ret;
	struct clk_programmable *prog = to_clk_programmable(hw);
	struct at91_pmc *pmc = prog->pmc;
	const struct clk_programmable_layout *layout = prog->layout;

	tmp = pmc_read(pmc, AT91_PMC_PCKR(prog->id));
	prog->css = tmp & layout->css_mask;
	ret = prog->css;
	if (layout->have_slck_mck) {
		prog->slckmck = !!(tmp & AT91_PMC_CSSMCK_MCK);
		if (prog->slckmck && !ret)
			ret = PROG_MAX_RM9200_CSS + 1;
	}

	return ret;
}

static int clk_programmable_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct clk_programmable *prog = to_clk_programmable(hw);
	unsigned long best_rate = parent_rate;
	unsigned long best_diff;
	unsigned long new_diff;
	unsigned long cur_rate;
	int shift = 0;

	if (rate > parent_rate)
		return parent_rate;
	else
		best_diff = parent_rate - rate;

	if (!best_diff) {
		prog->pres = shift;
		return 0;
	}

	for (shift = 1; shift < PROG_PRES_MASK; shift++) {
		cur_rate = parent_rate >> shift;

		if (cur_rate > rate)
			new_diff = cur_rate - rate;
		else
			new_diff = rate - cur_rate;

		if (!new_diff)
			break;

		if (new_diff < best_diff) {
			best_diff = new_diff;
			best_rate = cur_rate;
		}

		if (rate > cur_rate)
			break;
	}

	prog->pres = shift;
	return 0;
}

static const struct clk_ops programmable_ops = {
	.prepare = clk_programmable_prepare,
	.is_prepared = clk_programmable_is_ready,
	.recalc_rate = clk_programmable_recalc_rate,
	.round_rate = clk_programmable_round_rate,
	.get_parent = clk_programmable_get_parent,
	.set_parent = clk_programmable_set_parent,
	.set_rate = clk_programmable_set_rate,
};

static struct clk * __init
at91_clk_register_programmable(struct at91_pmc *pmc, unsigned int irq,
			       const char *name, const char **parent_names,
			       u8 num_parents, u8 id,
			       const struct clk_programmable_layout *layout)
{
	int ret;
	struct clk_programmable *prog;
	struct clk *clk = NULL;
	struct clk_init_data init;
	char irq_name[11];

	if (id > PROG_ID_MAX)
		return ERR_PTR(-EINVAL);

	prog = kzalloc(sizeof(*prog), GFP_KERNEL);
	if (!prog)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &programmable_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE;

	prog->id = id;
	prog->layout = layout;
	prog->hw.init = &init;
	prog->pmc = pmc;
	prog->irq = irq;
	init_waitqueue_head(&prog->wait);
	irq_set_status_flags(prog->irq, IRQ_NOAUTOEN);
	snprintf(irq_name, sizeof(irq_name), "clk-prog%d", id);
	ret = request_irq(prog->irq, clk_programmable_irq_handler,
			  IRQF_TRIGGER_HIGH, irq_name, prog);
	if (ret)
		return ERR_PTR(ret);

	clk = clk_register(NULL, &prog->hw);
	if (IS_ERR(clk))
		kfree(prog);

	return clk;
}

static const struct clk_programmable_layout at91rm9200_programmable_layout = {
	.pres_shift = 2,
	.css_mask = 0x3,
	.have_slck_mck = 0,
};

static const struct clk_programmable_layout at91sam9g45_programmable_layout = {
	.pres_shift = 2,
	.css_mask = 0x3,
	.have_slck_mck = 1,
};

static const struct clk_programmable_layout at91sam9x5_programmable_layout = {
	.pres_shift = 4,
	.css_mask = 0x7,
	.have_slck_mck = 0,
};

static void __init
of_at91_clk_prog_setup(struct device_node *np, struct at91_pmc *pmc,
		       const struct clk_programmable_layout *layout)
{
	int num;
	u32 id;
	int i;
	unsigned int irq;
	struct clk *clk;
	int num_parents;
	const char *parent_names[PROG_SOURCE_MAX];
	const char *name;
	struct device_node *progclknp;

	num_parents = of_count_phandle_with_args(np, "clocks", "#clock-cells");
	if (num_parents <= 0 || num_parents > PROG_SOURCE_MAX)
		return;

	for (i = 0; i < num_parents; ++i) {
		parent_names[i] = of_clk_get_parent_name(np, i);
		if (!parent_names[i])
			return;
	}

	num = of_get_child_count(np);
	if (!num || num > (PROG_ID_MAX + 1))
		return;

	for_each_child_of_node(np, progclknp) {
		if (of_property_read_u32(progclknp, "reg", &id))
			continue;

		if (of_property_read_string(np, "clock-output-names", &name))
			name = progclknp->name;

		irq = irq_of_parse_and_map(progclknp, 0);
		if (!irq)
			continue;

		clk = at91_clk_register_programmable(pmc, irq, name,
						     parent_names, num_parents,
						     id, layout);
		if (IS_ERR(clk))
			continue;

		of_clk_add_provider(progclknp, of_clk_src_simple_get, clk);
	}
}


void __init of_at91rm9200_clk_prog_setup(struct device_node *np,
					 struct at91_pmc *pmc)
{
	of_at91_clk_prog_setup(np, pmc, &at91rm9200_programmable_layout);
}

void __init of_at91sam9g45_clk_prog_setup(struct device_node *np,
					  struct at91_pmc *pmc)
{
	of_at91_clk_prog_setup(np, pmc, &at91sam9g45_programmable_layout);
}

void __init of_at91sam9x5_clk_prog_setup(struct device_node *np,
					 struct at91_pmc *pmc)
{
	of_at91_clk_prog_setup(np, pmc, &at91sam9x5_programmable_layout);
}
