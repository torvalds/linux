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
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include "pmc.h"

#define SYSTEM_MAX_ID		31

#define SYSTEM_MAX_NAME_SZ	32

#define to_clk_system(hw) container_of(hw, struct clk_system, hw)
struct clk_system {
	struct clk_hw hw;
	struct at91_pmc *pmc;
	unsigned int irq;
	wait_queue_head_t wait;
	u8 id;
};

static inline int is_pck(int id)
{
	return (id >= 8) && (id <= 15);
}
static irqreturn_t clk_system_irq_handler(int irq, void *dev_id)
{
	struct clk_system *sys = (struct clk_system *)dev_id;

	wake_up(&sys->wait);
	disable_irq_nosync(sys->irq);

	return IRQ_HANDLED;
}

static int clk_system_prepare(struct clk_hw *hw)
{
	struct clk_system *sys = to_clk_system(hw);
	struct at91_pmc *pmc = sys->pmc;
	u32 mask = 1 << sys->id;

	pmc_write(pmc, AT91_PMC_SCER, mask);

	if (!is_pck(sys->id))
		return 0;

	while (!(pmc_read(pmc, AT91_PMC_SR) & mask)) {
		if (sys->irq) {
			enable_irq(sys->irq);
			wait_event(sys->wait,
				   pmc_read(pmc, AT91_PMC_SR) & mask);
		} else
			cpu_relax();
	}
	return 0;
}

static void clk_system_unprepare(struct clk_hw *hw)
{
	struct clk_system *sys = to_clk_system(hw);
	struct at91_pmc *pmc = sys->pmc;

	pmc_write(pmc, AT91_PMC_SCDR, 1 << sys->id);
}

static int clk_system_is_prepared(struct clk_hw *hw)
{
	struct clk_system *sys = to_clk_system(hw);
	struct at91_pmc *pmc = sys->pmc;

	if (!(pmc_read(pmc, AT91_PMC_SCSR) & (1 << sys->id)))
		return 0;

	if (!is_pck(sys->id))
		return 1;

	return !!(pmc_read(pmc, AT91_PMC_SR) & (1 << sys->id));
}

static const struct clk_ops system_ops = {
	.prepare = clk_system_prepare,
	.unprepare = clk_system_unprepare,
	.is_prepared = clk_system_is_prepared,
};

static struct clk * __init
at91_clk_register_system(struct at91_pmc *pmc, const char *name,
			 const char *parent_name, u8 id, int irq)
{
	struct clk_system *sys;
	struct clk *clk = NULL;
	struct clk_init_data init;
	int ret;

	if (!parent_name || id > SYSTEM_MAX_ID)
		return ERR_PTR(-EINVAL);

	sys = kzalloc(sizeof(*sys), GFP_KERNEL);
	if (!sys)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &system_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	/*
	 * CLK_IGNORE_UNUSED is used to avoid ddrck switch off.
	 * TODO : we should implement a driver supporting at91 ddr controller
	 * (see drivers/memory) which would request and enable the ddrck clock.
	 * When this is done we will be able to remove CLK_IGNORE_UNUSED flag.
	 */
	init.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED;

	sys->id = id;
	sys->hw.init = &init;
	sys->pmc = pmc;
	sys->irq = irq;
	if (irq) {
		init_waitqueue_head(&sys->wait);
		irq_set_status_flags(sys->irq, IRQ_NOAUTOEN);
		ret = request_irq(sys->irq, clk_system_irq_handler,
				IRQF_TRIGGER_HIGH, name, sys);
		if (ret)
			return ERR_PTR(ret);
	}

	clk = clk_register(NULL, &sys->hw);
	if (IS_ERR(clk))
		kfree(sys);

	return clk;
}

static void __init
of_at91_clk_sys_setup(struct device_node *np, struct at91_pmc *pmc)
{
	int num;
	int irq = 0;
	u32 id;
	struct clk *clk;
	const char *name;
	struct device_node *sysclknp;
	const char *parent_name;

	num = of_get_child_count(np);
	if (num > (SYSTEM_MAX_ID + 1))
		return;

	for_each_child_of_node(np, sysclknp) {
		if (of_property_read_u32(sysclknp, "reg", &id))
			continue;

		if (of_property_read_string(np, "clock-output-names", &name))
			name = sysclknp->name;

		if (is_pck(id))
			irq = irq_of_parse_and_map(sysclknp, 0);

		parent_name = of_clk_get_parent_name(sysclknp, 0);

		clk = at91_clk_register_system(pmc, name, parent_name, id, irq);
		if (IS_ERR(clk))
			continue;

		of_clk_add_provider(sysclknp, of_clk_src_simple_get, clk);
	}
}

void __init of_at91rm9200_clk_sys_setup(struct device_node *np,
					struct at91_pmc *pmc)
{
	of_at91_clk_sys_setup(np, pmc);
}
