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

#define MOR_KEY_MASK		(0xff << 16)

struct clk_main_osc {
	struct clk_hw hw;
	struct at91_pmc *pmc;
	unsigned int irq;
	wait_queue_head_t wait;
};

#define to_clk_main_osc(hw) container_of(hw, struct clk_main_osc, hw)

struct clk_main_rc_osc {
	struct clk_hw hw;
	struct at91_pmc *pmc;
	unsigned int irq;
	wait_queue_head_t wait;
	unsigned long frequency;
	unsigned long accuracy;
};

#define to_clk_main_rc_osc(hw) container_of(hw, struct clk_main_rc_osc, hw)

struct clk_rm9200_main {
	struct clk_hw hw;
	struct at91_pmc *pmc;
};

#define to_clk_rm9200_main(hw) container_of(hw, struct clk_rm9200_main, hw)

struct clk_sam9x5_main {
	struct clk_hw hw;
	struct at91_pmc *pmc;
	unsigned int irq;
	wait_queue_head_t wait;
	u8 parent;
};

#define to_clk_sam9x5_main(hw) container_of(hw, struct clk_sam9x5_main, hw)

static irqreturn_t clk_main_osc_irq_handler(int irq, void *dev_id)
{
	struct clk_main_osc *osc = dev_id;

	wake_up(&osc->wait);
	disable_irq_nosync(osc->irq);

	return IRQ_HANDLED;
}

static int clk_main_osc_prepare(struct clk_hw *hw)
{
	struct clk_main_osc *osc = to_clk_main_osc(hw);
	struct at91_pmc *pmc = osc->pmc;
	u32 tmp;

	tmp = pmc_read(pmc, AT91_CKGR_MOR) & ~MOR_KEY_MASK;
	if (tmp & AT91_PMC_OSCBYPASS)
		return 0;

	if (!(tmp & AT91_PMC_MOSCEN)) {
		tmp |= AT91_PMC_MOSCEN | AT91_PMC_KEY;
		pmc_write(pmc, AT91_CKGR_MOR, tmp);
	}

	while (!(pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MOSCS)) {
		enable_irq(osc->irq);
		wait_event(osc->wait,
			   pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MOSCS);
	}

	return 0;
}

static void clk_main_osc_unprepare(struct clk_hw *hw)
{
	struct clk_main_osc *osc = to_clk_main_osc(hw);
	struct at91_pmc *pmc = osc->pmc;
	u32 tmp = pmc_read(pmc, AT91_CKGR_MOR);

	if (tmp & AT91_PMC_OSCBYPASS)
		return;

	if (!(tmp & AT91_PMC_MOSCEN))
		return;

	tmp &= ~(AT91_PMC_KEY | AT91_PMC_MOSCEN);
	pmc_write(pmc, AT91_CKGR_MOR, tmp | AT91_PMC_KEY);
}

static int clk_main_osc_is_prepared(struct clk_hw *hw)
{
	struct clk_main_osc *osc = to_clk_main_osc(hw);
	struct at91_pmc *pmc = osc->pmc;
	u32 tmp = pmc_read(pmc, AT91_CKGR_MOR);

	if (tmp & AT91_PMC_OSCBYPASS)
		return 1;

	return !!((pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MOSCS) &&
		  (pmc_read(pmc, AT91_CKGR_MOR) & AT91_PMC_MOSCEN));
}

static const struct clk_ops main_osc_ops = {
	.prepare = clk_main_osc_prepare,
	.unprepare = clk_main_osc_unprepare,
	.is_prepared = clk_main_osc_is_prepared,
};

static struct clk * __init
at91_clk_register_main_osc(struct at91_pmc *pmc,
			   unsigned int irq,
			   const char *name,
			   const char *parent_name,
			   bool bypass)
{
	int ret;
	struct clk_main_osc *osc;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!pmc || !irq || !name || !parent_name)
		return ERR_PTR(-EINVAL);

	osc = kzalloc(sizeof(*osc), GFP_KERNEL);
	if (!osc)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &main_osc_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_IGNORE_UNUSED;

	osc->hw.init = &init;
	osc->pmc = pmc;
	osc->irq = irq;

	init_waitqueue_head(&osc->wait);
	irq_set_status_flags(osc->irq, IRQ_NOAUTOEN);
	ret = request_irq(osc->irq, clk_main_osc_irq_handler,
			  IRQF_TRIGGER_HIGH, name, osc);
	if (ret)
		return ERR_PTR(ret);

	if (bypass)
		pmc_write(pmc, AT91_CKGR_MOR,
			  (pmc_read(pmc, AT91_CKGR_MOR) &
			   ~(MOR_KEY_MASK | AT91_PMC_MOSCEN)) |
			  AT91_PMC_OSCBYPASS | AT91_PMC_KEY);

	clk = clk_register(NULL, &osc->hw);
	if (IS_ERR(clk)) {
		free_irq(irq, osc);
		kfree(osc);
	}

	return clk;
}

void __init of_at91rm9200_clk_main_osc_setup(struct device_node *np,
					     struct at91_pmc *pmc)
{
	struct clk *clk;
	unsigned int irq;
	const char *name = np->name;
	const char *parent_name;
	bool bypass;

	of_property_read_string(np, "clock-output-names", &name);
	bypass = of_property_read_bool(np, "atmel,osc-bypass");
	parent_name = of_clk_get_parent_name(np, 0);

	irq = irq_of_parse_and_map(np, 0);
	if (!irq)
		return;

	clk = at91_clk_register_main_osc(pmc, irq, name, parent_name, bypass);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static irqreturn_t clk_main_rc_osc_irq_handler(int irq, void *dev_id)
{
	struct clk_main_rc_osc *osc = dev_id;

	wake_up(&osc->wait);
	disable_irq_nosync(osc->irq);

	return IRQ_HANDLED;
}

static int clk_main_rc_osc_prepare(struct clk_hw *hw)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);
	struct at91_pmc *pmc = osc->pmc;
	u32 tmp;

	tmp = pmc_read(pmc, AT91_CKGR_MOR) & ~MOR_KEY_MASK;

	if (!(tmp & AT91_PMC_MOSCRCEN)) {
		tmp |= AT91_PMC_MOSCRCEN | AT91_PMC_KEY;
		pmc_write(pmc, AT91_CKGR_MOR, tmp);
	}

	while (!(pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MOSCRCS)) {
		enable_irq(osc->irq);
		wait_event(osc->wait,
			   pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MOSCRCS);
	}

	return 0;
}

static void clk_main_rc_osc_unprepare(struct clk_hw *hw)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);
	struct at91_pmc *pmc = osc->pmc;
	u32 tmp = pmc_read(pmc, AT91_CKGR_MOR);

	if (!(tmp & AT91_PMC_MOSCRCEN))
		return;

	tmp &= ~(MOR_KEY_MASK | AT91_PMC_MOSCRCEN);
	pmc_write(pmc, AT91_CKGR_MOR, tmp | AT91_PMC_KEY);
}

static int clk_main_rc_osc_is_prepared(struct clk_hw *hw)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);
	struct at91_pmc *pmc = osc->pmc;

	return !!((pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MOSCRCS) &&
		  (pmc_read(pmc, AT91_CKGR_MOR) & AT91_PMC_MOSCRCEN));
}

static unsigned long clk_main_rc_osc_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);

	return osc->frequency;
}

static unsigned long clk_main_rc_osc_recalc_accuracy(struct clk_hw *hw,
						     unsigned long parent_acc)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);

	return osc->accuracy;
}

static const struct clk_ops main_rc_osc_ops = {
	.prepare = clk_main_rc_osc_prepare,
	.unprepare = clk_main_rc_osc_unprepare,
	.is_prepared = clk_main_rc_osc_is_prepared,
	.recalc_rate = clk_main_rc_osc_recalc_rate,
	.recalc_accuracy = clk_main_rc_osc_recalc_accuracy,
};

static struct clk * __init
at91_clk_register_main_rc_osc(struct at91_pmc *pmc,
			      unsigned int irq,
			      const char *name,
			      u32 frequency, u32 accuracy)
{
	int ret;
	struct clk_main_rc_osc *osc;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!pmc || !irq || !name || !frequency)
		return ERR_PTR(-EINVAL);

	osc = kzalloc(sizeof(*osc), GFP_KERNEL);
	if (!osc)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &main_rc_osc_ops;
	init.parent_names = NULL;
	init.num_parents = 0;
	init.flags = CLK_IS_ROOT | CLK_IGNORE_UNUSED;

	osc->hw.init = &init;
	osc->pmc = pmc;
	osc->irq = irq;
	osc->frequency = frequency;
	osc->accuracy = accuracy;

	init_waitqueue_head(&osc->wait);
	irq_set_status_flags(osc->irq, IRQ_NOAUTOEN);
	ret = request_irq(osc->irq, clk_main_rc_osc_irq_handler,
			  IRQF_TRIGGER_HIGH, name, osc);
	if (ret)
		return ERR_PTR(ret);

	clk = clk_register(NULL, &osc->hw);
	if (IS_ERR(clk)) {
		free_irq(irq, osc);
		kfree(osc);
	}

	return clk;
}

void __init of_at91sam9x5_clk_main_rc_osc_setup(struct device_node *np,
						struct at91_pmc *pmc)
{
	struct clk *clk;
	unsigned int irq;
	u32 frequency = 0;
	u32 accuracy = 0;
	const char *name = np->name;

	of_property_read_string(np, "clock-output-names", &name);
	of_property_read_u32(np, "clock-frequency", &frequency);
	of_property_read_u32(np, "clock-accuracy", &accuracy);

	irq = irq_of_parse_and_map(np, 0);
	if (!irq)
		return;

	clk = at91_clk_register_main_rc_osc(pmc, irq, name, frequency,
					    accuracy);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}


static int clk_main_probe_frequency(struct at91_pmc *pmc)
{
	unsigned long prep_time, timeout;
	u32 tmp;

	timeout = jiffies + usecs_to_jiffies(MAINFRDY_TIMEOUT);
	do {
		prep_time = jiffies;
		tmp = pmc_read(pmc, AT91_CKGR_MCFR);
		if (tmp & AT91_PMC_MAINRDY)
			return 0;
		usleep_range(MAINF_LOOP_MIN_WAIT, MAINF_LOOP_MAX_WAIT);
	} while (time_before(prep_time, timeout));

	return -ETIMEDOUT;
}

static unsigned long clk_main_recalc_rate(struct at91_pmc *pmc,
					  unsigned long parent_rate)
{
	u32 tmp;

	if (parent_rate)
		return parent_rate;

	tmp = pmc_read(pmc, AT91_CKGR_MCFR);
	if (!(tmp & AT91_PMC_MAINRDY))
		return 0;

	return ((tmp & AT91_PMC_MAINF) * SLOW_CLOCK_FREQ) / MAINF_DIV;
}

static int clk_rm9200_main_prepare(struct clk_hw *hw)
{
	struct clk_rm9200_main *clkmain = to_clk_rm9200_main(hw);

	return clk_main_probe_frequency(clkmain->pmc);
}

static int clk_rm9200_main_is_prepared(struct clk_hw *hw)
{
	struct clk_rm9200_main *clkmain = to_clk_rm9200_main(hw);

	return !!(pmc_read(clkmain->pmc, AT91_CKGR_MCFR) & AT91_PMC_MAINRDY);
}

static unsigned long clk_rm9200_main_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_rm9200_main *clkmain = to_clk_rm9200_main(hw);

	return clk_main_recalc_rate(clkmain->pmc, parent_rate);
}

static const struct clk_ops rm9200_main_ops = {
	.prepare = clk_rm9200_main_prepare,
	.is_prepared = clk_rm9200_main_is_prepared,
	.recalc_rate = clk_rm9200_main_recalc_rate,
};

static struct clk * __init
at91_clk_register_rm9200_main(struct at91_pmc *pmc,
			      const char *name,
			      const char *parent_name)
{
	struct clk_rm9200_main *clkmain;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!pmc || !name)
		return ERR_PTR(-EINVAL);

	if (!parent_name)
		return ERR_PTR(-EINVAL);

	clkmain = kzalloc(sizeof(*clkmain), GFP_KERNEL);
	if (!clkmain)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &rm9200_main_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = 0;

	clkmain->hw.init = &init;
	clkmain->pmc = pmc;

	clk = clk_register(NULL, &clkmain->hw);
	if (IS_ERR(clk))
		kfree(clkmain);

	return clk;
}

void __init of_at91rm9200_clk_main_setup(struct device_node *np,
					 struct at91_pmc *pmc)
{
	struct clk *clk;
	const char *parent_name;
	const char *name = np->name;

	parent_name = of_clk_get_parent_name(np, 0);
	of_property_read_string(np, "clock-output-names", &name);

	clk = at91_clk_register_rm9200_main(pmc, name, parent_name);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static irqreturn_t clk_sam9x5_main_irq_handler(int irq, void *dev_id)
{
	struct clk_sam9x5_main *clkmain = dev_id;

	wake_up(&clkmain->wait);
	disable_irq_nosync(clkmain->irq);

	return IRQ_HANDLED;
}

static int clk_sam9x5_main_prepare(struct clk_hw *hw)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);
	struct at91_pmc *pmc = clkmain->pmc;

	while (!(pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MOSCSELS)) {
		enable_irq(clkmain->irq);
		wait_event(clkmain->wait,
			   pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MOSCSELS);
	}

	return clk_main_probe_frequency(pmc);
}

static int clk_sam9x5_main_is_prepared(struct clk_hw *hw)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);

	return !!(pmc_read(clkmain->pmc, AT91_PMC_SR) & AT91_PMC_MOSCSELS);
}

static unsigned long clk_sam9x5_main_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);

	return clk_main_recalc_rate(clkmain->pmc, parent_rate);
}

static int clk_sam9x5_main_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);
	struct at91_pmc *pmc = clkmain->pmc;
	u32 tmp;

	if (index > 1)
		return -EINVAL;

	tmp = pmc_read(pmc, AT91_CKGR_MOR) & ~MOR_KEY_MASK;

	if (index && !(tmp & AT91_PMC_MOSCSEL))
		pmc_write(pmc, AT91_CKGR_MOR, tmp | AT91_PMC_MOSCSEL);
	else if (!index && (tmp & AT91_PMC_MOSCSEL))
		pmc_write(pmc, AT91_CKGR_MOR, tmp & ~AT91_PMC_MOSCSEL);

	while (!(pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MOSCSELS)) {
		enable_irq(clkmain->irq);
		wait_event(clkmain->wait,
			   pmc_read(pmc, AT91_PMC_SR) & AT91_PMC_MOSCSELS);
	}

	return 0;
}

static u8 clk_sam9x5_main_get_parent(struct clk_hw *hw)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);

	return !!(pmc_read(clkmain->pmc, AT91_CKGR_MOR) & AT91_PMC_MOSCEN);
}

static const struct clk_ops sam9x5_main_ops = {
	.prepare = clk_sam9x5_main_prepare,
	.is_prepared = clk_sam9x5_main_is_prepared,
	.recalc_rate = clk_sam9x5_main_recalc_rate,
	.set_parent = clk_sam9x5_main_set_parent,
	.get_parent = clk_sam9x5_main_get_parent,
};

static struct clk * __init
at91_clk_register_sam9x5_main(struct at91_pmc *pmc,
			      unsigned int irq,
			      const char *name,
			      const char **parent_names,
			      int num_parents)
{
	int ret;
	struct clk_sam9x5_main *clkmain;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!pmc || !irq || !name)
		return ERR_PTR(-EINVAL);

	if (!parent_names || !num_parents)
		return ERR_PTR(-EINVAL);

	clkmain = kzalloc(sizeof(*clkmain), GFP_KERNEL);
	if (!clkmain)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &sam9x5_main_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_SET_PARENT_GATE;

	clkmain->hw.init = &init;
	clkmain->pmc = pmc;
	clkmain->irq = irq;
	clkmain->parent = !!(pmc_read(clkmain->pmc, AT91_CKGR_MOR) &
			     AT91_PMC_MOSCEN);
	init_waitqueue_head(&clkmain->wait);
	irq_set_status_flags(clkmain->irq, IRQ_NOAUTOEN);
	ret = request_irq(clkmain->irq, clk_sam9x5_main_irq_handler,
			  IRQF_TRIGGER_HIGH, name, clkmain);
	if (ret)
		return ERR_PTR(ret);

	clk = clk_register(NULL, &clkmain->hw);
	if (IS_ERR(clk)) {
		free_irq(clkmain->irq, clkmain);
		kfree(clkmain);
	}

	return clk;
}

void __init of_at91sam9x5_clk_main_setup(struct device_node *np,
					 struct at91_pmc *pmc)
{
	struct clk *clk;
	const char *parent_names[2];
	int num_parents;
	unsigned int irq;
	const char *name = np->name;
	int i;

	num_parents = of_count_phandle_with_args(np, "clocks", "#clock-cells");
	if (num_parents <= 0 || num_parents > 2)
		return;

	for (i = 0; i < num_parents; ++i) {
		parent_names[i] = of_clk_get_parent_name(np, i);
		if (!parent_names[i])
			return;
	}

	of_property_read_string(np, "clock-output-names", &name);

	irq = irq_of_parse_and_map(np, 0);
	if (!irq)
		return;

	clk = at91_clk_register_sam9x5_main(pmc, irq, name, parent_names,
					    num_parents);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}
