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
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "pmc.h"

#define SLOW_CLOCK_FREQ		32768
#define MAINF_DIV		16
#define MAINFRDY_TIMEOUT	(((MAINF_DIV + 1) * USEC_PER_SEC) / \
				 SLOW_CLOCK_FREQ)
#define MAINF_LOOP_MIN_WAIT	(USEC_PER_SEC / SLOW_CLOCK_FREQ)
#define MAINF_LOOP_MAX_WAIT	MAINFRDY_TIMEOUT

struct clk_main {
	struct clk_hw hw;
	struct at91_pmc *pmc;
	unsigned long rate;
	unsigned int irq;
	wait_queue_head_t wait;
};

#define to_clk_main(hw) container_of(hw, struct clk_main, hw)

static irqreturn_t clk_main_irq_handler(int irq, void *dev_id)
{
	struct clk_main *clkmain = (struct clk_main *)dev_id;

	wake_up(&clkmain->wait);
	disable_irq_nosync(clkmain->irq);

	return IRQ_HANDLED;
}

static int clk_main_prepare(struct clk_hw *hw)
{
	struct clk_main *clkmain = to_clk_main(hw);
	struct at91_pmc *pmc = clkmain->pmc;
	unsigned long halt_time, timeout;
	u32 tmp;

	while (!(pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MOSCS)) {
		enable_irq(clkmain->irq);
		wait_event(clkmain->wait,
			   pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MOSCS);
	}

	if (clkmain->rate)
		return 0;

	timeout = jiffies + usecs_to_jiffies(MAINFRDY_TIMEOUT);
	do {
		halt_time = jiffies;
		tmp = pmc_read(pmc, AT91_CKGR_MCFR);
		if (tmp & AT91_PMC_MAINRDY)
			return 0;
		usleep_range(MAINF_LOOP_MIN_WAIT, MAINF_LOOP_MAX_WAIT);
	} while (time_before(halt_time, timeout));

	return 0;
}

static int clk_main_is_prepared(struct clk_hw *hw)
{
	struct clk_main *clkmain = to_clk_main(hw);

	return !!(pmc_read(clkmain->pmc, AT91_PMC_SR) & AT91_PMC_MOSCS);
}

static unsigned long clk_main_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	u32 tmp;
	struct clk_main *clkmain = to_clk_main(hw);
	struct at91_pmc *pmc = clkmain->pmc;

	if (clkmain->rate)
		return clkmain->rate;

	tmp = pmc_read(pmc, AT91_CKGR_MCFR) & AT91_PMC_MAINF;
	clkmain->rate = (tmp * parent_rate) / MAINF_DIV;

	return clkmain->rate;
}

static const struct clk_ops main_ops = {
	.prepare = clk_main_prepare,
	.is_prepared = clk_main_is_prepared,
	.recalc_rate = clk_main_recalc_rate,
};

static struct clk * __init
at91_clk_register_main(struct at91_pmc *pmc,
		       unsigned int irq,
		       const char *name,
		       const char *parent_name,
		       unsigned long rate)
{
	int ret;
	struct clk_main *clkmain;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!pmc || !irq || !name)
		return ERR_PTR(-EINVAL);

	if (!rate && !parent_name)
		return ERR_PTR(-EINVAL);

	clkmain = kzalloc(sizeof(*clkmain), GFP_KERNEL);
	if (!clkmain)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &main_ops;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;
	init.flags = parent_name ? 0 : CLK_IS_ROOT;

	clkmain->hw.init = &init;
	clkmain->rate = rate;
	clkmain->pmc = pmc;
	clkmain->irq = irq;
	init_waitqueue_head(&clkmain->wait);
	irq_set_status_flags(clkmain->irq, IRQ_NOAUTOEN);
	ret = request_irq(clkmain->irq, clk_main_irq_handler,
			  IRQF_TRIGGER_HIGH, "clk-main", clkmain);
	if (ret)
		return ERR_PTR(ret);

	clk = clk_register(NULL, &clkmain->hw);
	if (IS_ERR(clk)) {
		free_irq(clkmain->irq, clkmain);
		kfree(clkmain);
	}

	return clk;
}



static void __init
of_at91_clk_main_setup(struct device_node *np, struct at91_pmc *pmc)
{
	struct clk *clk;
	unsigned int irq;
	const char *parent_name;
	const char *name = np->name;
	u32 rate = 0;

	parent_name = of_clk_get_parent_name(np, 0);
	of_property_read_string(np, "clock-output-names", &name);
	of_property_read_u32(np, "clock-frequency", &rate);
	irq = irq_of_parse_and_map(np, 0);
	if (!irq)
		return;

	clk = at91_clk_register_main(pmc, irq, name, parent_name, rate);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

void __init of_at91rm9200_clk_main_setup(struct device_node *np,
					 struct at91_pmc *pmc)
{
	of_at91_clk_main_setup(np, pmc);
}
