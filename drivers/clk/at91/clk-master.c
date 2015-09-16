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

#define MASTER_SOURCE_MAX	4

#define MASTER_PRES_MASK	0x7
#define MASTER_PRES_MAX		MASTER_PRES_MASK
#define MASTER_DIV_SHIFT	8
#define MASTER_DIV_MASK		0x3

struct clk_master_characteristics {
	struct clk_range output;
	u32 divisors[4];
	u8 have_div3_pres;
};

struct clk_master_layout {
	u32 mask;
	u8 pres_shift;
};

#define to_clk_master(hw) container_of(hw, struct clk_master, hw)

struct clk_master {
	struct clk_hw hw;
	struct at91_pmc *pmc;
	unsigned int irq;
	wait_queue_head_t wait;
	const struct clk_master_layout *layout;
	const struct clk_master_characteristics *characteristics;
};

static irqreturn_t clk_master_irq_handler(int irq, void *dev_id)
{
	struct clk_master *master = (struct clk_master *)dev_id;

	wake_up(&master->wait);
	disable_irq_nosync(master->irq);

	return IRQ_HANDLED;
}
static int clk_master_prepare(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);
	struct at91_pmc *pmc = master->pmc;

	while (!(pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MCKRDY)) {
		enable_irq(master->irq);
		wait_event(master->wait,
			   pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MCKRDY);
	}

	return 0;
}

static int clk_master_is_prepared(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);

	return !!(pmc_read(master->pmc, AT91_PMC_SR) & AT91_PMC_MCKRDY);
}

static unsigned long clk_master_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	u8 pres;
	u8 div;
	unsigned long rate = parent_rate;
	struct clk_master *master = to_clk_master(hw);
	struct at91_pmc *pmc = master->pmc;
	const struct clk_master_layout *layout = master->layout;
	const struct clk_master_characteristics *characteristics =
						master->characteristics;
	u32 tmp;

	pmc_lock(pmc);
	tmp = pmc_read(pmc, AT91_PMC_MCKR) & layout->mask;
	pmc_unlock(pmc);

	pres = (tmp >> layout->pres_shift) & MASTER_PRES_MASK;
	div = (tmp >> MASTER_DIV_SHIFT) & MASTER_DIV_MASK;

	if (characteristics->have_div3_pres && pres == MASTER_PRES_MAX)
		rate /= 3;
	else
		rate >>= pres;

	rate /= characteristics->divisors[div];

	if (rate < characteristics->output.min)
		pr_warn("master clk is underclocked");
	else if (rate > characteristics->output.max)
		pr_warn("master clk is overclocked");

	return rate;
}

static u8 clk_master_get_parent(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);
	struct at91_pmc *pmc = master->pmc;

	return pmc_read(pmc, AT91_PMC_MCKR) & AT91_PMC_CSS;
}

static const struct clk_ops master_ops = {
	.prepare = clk_master_prepare,
	.is_prepared = clk_master_is_prepared,
	.recalc_rate = clk_master_recalc_rate,
	.get_parent = clk_master_get_parent,
};

static struct clk * __init
at91_clk_register_master(struct at91_pmc *pmc, unsigned int irq,
		const char *name, int num_parents,
		const char **parent_names,
		const struct clk_master_layout *layout,
		const struct clk_master_characteristics *characteristics)
{
	int ret;
	struct clk_master *master;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!pmc || !irq || !name || !num_parents || !parent_names)
		return ERR_PTR(-EINVAL);

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &master_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;

	master->hw.init = &init;
	master->layout = layout;
	master->characteristics = characteristics;
	master->pmc = pmc;
	master->irq = irq;
	init_waitqueue_head(&master->wait);
	irq_set_status_flags(master->irq, IRQ_NOAUTOEN);
	ret = request_irq(master->irq, clk_master_irq_handler,
			  IRQF_TRIGGER_HIGH, "clk-master", master);
	if (ret) {
		kfree(master);
		return ERR_PTR(ret);
	}

	clk = clk_register(NULL, &master->hw);
	if (IS_ERR(clk)) {
		free_irq(master->irq, master);
		kfree(master);
	}

	return clk;
}


static const struct clk_master_layout at91rm9200_master_layout = {
	.mask = 0x31F,
	.pres_shift = 2,
};

static const struct clk_master_layout at91sam9x5_master_layout = {
	.mask = 0x373,
	.pres_shift = 4,
};


static struct clk_master_characteristics * __init
of_at91_clk_master_get_characteristics(struct device_node *np)
{
	struct clk_master_characteristics *characteristics;

	characteristics = kzalloc(sizeof(*characteristics), GFP_KERNEL);
	if (!characteristics)
		return NULL;

	if (of_at91_get_clk_range(np, "atmel,clk-output-range", &characteristics->output))
		goto out_free_characteristics;

	of_property_read_u32_array(np, "atmel,clk-divisors",
				   characteristics->divisors, 4);

	characteristics->have_div3_pres =
		of_property_read_bool(np, "atmel,master-clk-have-div3-pres");

	return characteristics;

out_free_characteristics:
	kfree(characteristics);
	return NULL;
}

static void __init
of_at91_clk_master_setup(struct device_node *np, struct at91_pmc *pmc,
			 const struct clk_master_layout *layout)
{
	struct clk *clk;
	int num_parents;
	unsigned int irq;
	const char *parent_names[MASTER_SOURCE_MAX];
	const char *name = np->name;
	struct clk_master_characteristics *characteristics;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents <= 0 || num_parents > MASTER_SOURCE_MAX)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);

	of_property_read_string(np, "clock-output-names", &name);

	characteristics = of_at91_clk_master_get_characteristics(np);
	if (!characteristics)
		return;

	irq = irq_of_parse_and_map(np, 0);
	if (!irq)
		goto out_free_characteristics;

	clk = at91_clk_register_master(pmc, irq, name, num_parents,
				       parent_names, layout,
				       characteristics);
	if (IS_ERR(clk))
		goto out_free_characteristics;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
	return;

out_free_characteristics:
	kfree(characteristics);
}

void __init of_at91rm9200_clk_master_setup(struct device_node *np,
					   struct at91_pmc *pmc)
{
	of_at91_clk_master_setup(np, pmc, &at91rm9200_master_layout);
}

void __init of_at91sam9x5_clk_master_setup(struct device_node *np,
					   struct at91_pmc *pmc)
{
	of_at91_clk_master_setup(np, pmc, &at91sam9x5_master_layout);
}
