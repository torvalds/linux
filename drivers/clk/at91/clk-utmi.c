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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "pmc.h"

#define UTMI_FIXED_MUL		40

struct clk_utmi {
	struct clk_hw hw;
	struct at91_pmc *pmc;
	unsigned int irq;
	wait_queue_head_t wait;
};

#define to_clk_utmi(hw) container_of(hw, struct clk_utmi, hw)

static irqreturn_t clk_utmi_irq_handler(int irq, void *dev_id)
{
	struct clk_utmi *utmi = (struct clk_utmi *)dev_id;

	wake_up(&utmi->wait);
	disable_irq_nosync(utmi->irq);

	return IRQ_HANDLED;
}

static int clk_utmi_prepare(struct clk_hw *hw)
{
	struct clk_utmi *utmi = to_clk_utmi(hw);
	struct at91_pmc *pmc = utmi->pmc;
	u32 tmp = pmc_read(pmc, AT91_CKGR_UCKR) | AT91_PMC_UPLLEN |
		  AT91_PMC_UPLLCOUNT | AT91_PMC_BIASEN;

	pmc_write(pmc, AT91_CKGR_UCKR, tmp);

	while (!(pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_LOCKU)) {
		enable_irq(utmi->irq);
		wait_event(utmi->wait,
			   pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_LOCKU);
	}

	return 0;
}

static int clk_utmi_is_prepared(struct clk_hw *hw)
{
	struct clk_utmi *utmi = to_clk_utmi(hw);
	struct at91_pmc *pmc = utmi->pmc;

	return !!(pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_LOCKU);
}

static void clk_utmi_unprepare(struct clk_hw *hw)
{
	struct clk_utmi *utmi = to_clk_utmi(hw);
	struct at91_pmc *pmc = utmi->pmc;
	u32 tmp = pmc_read(pmc, AT91_CKGR_UCKR) & ~AT91_PMC_UPLLEN;

	pmc_write(pmc, AT91_CKGR_UCKR, tmp);
}

static unsigned long clk_utmi_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	/* UTMI clk is a fixed clk multiplier */
	return parent_rate * UTMI_FIXED_MUL;
}

static const struct clk_ops utmi_ops = {
	.prepare = clk_utmi_prepare,
	.unprepare = clk_utmi_unprepare,
	.is_prepared = clk_utmi_is_prepared,
	.recalc_rate = clk_utmi_recalc_rate,
};

static struct clk * __init
at91_clk_register_utmi(struct at91_pmc *pmc, unsigned int irq,
		       const char *name, const char *parent_name)
{
	int ret;
	struct clk_utmi *utmi;
	struct clk *clk = NULL;
	struct clk_init_data init;

	utmi = kzalloc(sizeof(*utmi), GFP_KERNEL);
	if (!utmi)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &utmi_ops;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;
	init.flags = CLK_SET_RATE_GATE;

	utmi->hw.init = &init;
	utmi->pmc = pmc;
	utmi->irq = irq;
	init_waitqueue_head(&utmi->wait);
	irq_set_status_flags(utmi->irq, IRQ_NOAUTOEN);
	ret = request_irq(utmi->irq, clk_utmi_irq_handler,
			  IRQF_TRIGGER_HIGH, "clk-utmi", utmi);
	if (ret) {
		kfree(utmi);
		return ERR_PTR(ret);
	}

	clk = clk_register(NULL, &utmi->hw);
	if (IS_ERR(clk)) {
		free_irq(utmi->irq, utmi);
		kfree(utmi);
	}

	return clk;
}

static void __init
of_at91_clk_utmi_setup(struct device_node *np, struct at91_pmc *pmc)
{
	unsigned int irq;
	struct clk *clk;
	const char *parent_name;
	const char *name = np->name;

	parent_name = of_clk_get_parent_name(np, 0);

	of_property_read_string(np, "clock-output-names", &name);

	irq = irq_of_parse_and_map(np, 0);
	if (!irq)
		return;

	clk = at91_clk_register_utmi(pmc, irq, name, parent_name);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
	return;
}

void __init of_at91sam9x5_clk_utmi_setup(struct device_node *np,
					 struct at91_pmc *pmc)
{
	of_at91_clk_utmi_setup(np, pmc);
}
