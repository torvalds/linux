/*
 * drivers/clk/at91/clk-slow.c
 *
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
#include "sckc.h"

#define SLOW_CLOCK_FREQ		32768
#define SLOWCK_SW_CYCLES	5
#define SLOWCK_SW_TIME_USEC	((SLOWCK_SW_CYCLES * USEC_PER_SEC) / \
				 SLOW_CLOCK_FREQ)

#define	AT91_SCKC_CR			0x00
#define		AT91_SCKC_RCEN		(1 << 0)
#define		AT91_SCKC_OSC32EN	(1 << 1)
#define		AT91_SCKC_OSC32BYP	(1 << 2)
#define		AT91_SCKC_OSCSEL	(1 << 3)

struct clk_slow_osc {
	struct clk_hw hw;
	void __iomem *sckcr;
	unsigned long startup_usec;
};

#define to_clk_slow_osc(hw) container_of(hw, struct clk_slow_osc, hw)

struct clk_slow_rc_osc {
	struct clk_hw hw;
	void __iomem *sckcr;
	unsigned long frequency;
	unsigned long accuracy;
	unsigned long startup_usec;
};

#define to_clk_slow_rc_osc(hw) container_of(hw, struct clk_slow_rc_osc, hw)

struct clk_sam9260_slow {
	struct clk_hw hw;
	struct at91_pmc *pmc;
};

#define to_clk_sam9260_slow(hw) container_of(hw, struct clk_sam9260_slow, hw)

struct clk_sam9x5_slow {
	struct clk_hw hw;
	void __iomem *sckcr;
	u8 parent;
};

#define to_clk_sam9x5_slow(hw) container_of(hw, struct clk_sam9x5_slow, hw)

static struct clk *slow_clk;

static int clk_slow_osc_prepare(struct clk_hw *hw)
{
	struct clk_slow_osc *osc = to_clk_slow_osc(hw);
	void __iomem *sckcr = osc->sckcr;
	u32 tmp = readl(sckcr);

	if (tmp & AT91_SCKC_OSC32BYP)
		return 0;

	writel(tmp | AT91_SCKC_OSC32EN, sckcr);

	usleep_range(osc->startup_usec, osc->startup_usec + 1);

	return 0;
}

static void clk_slow_osc_unprepare(struct clk_hw *hw)
{
	struct clk_slow_osc *osc = to_clk_slow_osc(hw);
	void __iomem *sckcr = osc->sckcr;
	u32 tmp = readl(sckcr);

	if (tmp & AT91_SCKC_OSC32BYP)
		return;

	writel(tmp & ~AT91_SCKC_OSC32EN, sckcr);
}

static int clk_slow_osc_is_prepared(struct clk_hw *hw)
{
	struct clk_slow_osc *osc = to_clk_slow_osc(hw);
	void __iomem *sckcr = osc->sckcr;
	u32 tmp = readl(sckcr);

	if (tmp & AT91_SCKC_OSC32BYP)
		return 1;

	return !!(tmp & AT91_SCKC_OSC32EN);
}

static const struct clk_ops slow_osc_ops = {
	.prepare = clk_slow_osc_prepare,
	.unprepare = clk_slow_osc_unprepare,
	.is_prepared = clk_slow_osc_is_prepared,
};

static struct clk * __init
at91_clk_register_slow_osc(void __iomem *sckcr,
			   const char *name,
			   const char *parent_name,
			   unsigned long startup,
			   bool bypass)
{
	struct clk_slow_osc *osc;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!sckcr || !name || !parent_name)
		return ERR_PTR(-EINVAL);

	osc = kzalloc(sizeof(*osc), GFP_KERNEL);
	if (!osc)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &slow_osc_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_IGNORE_UNUSED;

	osc->hw.init = &init;
	osc->sckcr = sckcr;
	osc->startup_usec = startup;

	if (bypass)
		writel((readl(sckcr) & ~AT91_SCKC_OSC32EN) | AT91_SCKC_OSC32BYP,
		       sckcr);

	clk = clk_register(NULL, &osc->hw);
	if (IS_ERR(clk))
		kfree(osc);

	return clk;
}

void __init of_at91sam9x5_clk_slow_osc_setup(struct device_node *np,
					     void __iomem *sckcr)
{
	struct clk *clk;
	const char *parent_name;
	const char *name = np->name;
	u32 startup;
	bool bypass;

	parent_name = of_clk_get_parent_name(np, 0);
	of_property_read_string(np, "clock-output-names", &name);
	of_property_read_u32(np, "atmel,startup-time-usec", &startup);
	bypass = of_property_read_bool(np, "atmel,osc-bypass");

	clk = at91_clk_register_slow_osc(sckcr, name, parent_name, startup,
					 bypass);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static unsigned long clk_slow_rc_osc_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_slow_rc_osc *osc = to_clk_slow_rc_osc(hw);

	return osc->frequency;
}

static unsigned long clk_slow_rc_osc_recalc_accuracy(struct clk_hw *hw,
						     unsigned long parent_acc)
{
	struct clk_slow_rc_osc *osc = to_clk_slow_rc_osc(hw);

	return osc->accuracy;
}

static int clk_slow_rc_osc_prepare(struct clk_hw *hw)
{
	struct clk_slow_rc_osc *osc = to_clk_slow_rc_osc(hw);
	void __iomem *sckcr = osc->sckcr;

	writel(readl(sckcr) | AT91_SCKC_RCEN, sckcr);

	usleep_range(osc->startup_usec, osc->startup_usec + 1);

	return 0;
}

static void clk_slow_rc_osc_unprepare(struct clk_hw *hw)
{
	struct clk_slow_rc_osc *osc = to_clk_slow_rc_osc(hw);
	void __iomem *sckcr = osc->sckcr;

	writel(readl(sckcr) & ~AT91_SCKC_RCEN, sckcr);
}

static int clk_slow_rc_osc_is_prepared(struct clk_hw *hw)
{
	struct clk_slow_rc_osc *osc = to_clk_slow_rc_osc(hw);

	return !!(readl(osc->sckcr) & AT91_SCKC_RCEN);
}

static const struct clk_ops slow_rc_osc_ops = {
	.prepare = clk_slow_rc_osc_prepare,
	.unprepare = clk_slow_rc_osc_unprepare,
	.is_prepared = clk_slow_rc_osc_is_prepared,
	.recalc_rate = clk_slow_rc_osc_recalc_rate,
	.recalc_accuracy = clk_slow_rc_osc_recalc_accuracy,
};

static struct clk * __init
at91_clk_register_slow_rc_osc(void __iomem *sckcr,
			      const char *name,
			      unsigned long frequency,
			      unsigned long accuracy,
			      unsigned long startup)
{
	struct clk_slow_rc_osc *osc;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!sckcr || !name)
		return ERR_PTR(-EINVAL);

	osc = kzalloc(sizeof(*osc), GFP_KERNEL);
	if (!osc)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &slow_rc_osc_ops;
	init.parent_names = NULL;
	init.num_parents = 0;
	init.flags = CLK_IS_ROOT | CLK_IGNORE_UNUSED;

	osc->hw.init = &init;
	osc->sckcr = sckcr;
	osc->frequency = frequency;
	osc->accuracy = accuracy;
	osc->startup_usec = startup;

	clk = clk_register(NULL, &osc->hw);
	if (IS_ERR(clk))
		kfree(osc);

	return clk;
}

void __init of_at91sam9x5_clk_slow_rc_osc_setup(struct device_node *np,
						void __iomem *sckcr)
{
	struct clk *clk;
	u32 frequency = 0;
	u32 accuracy = 0;
	u32 startup = 0;
	const char *name = np->name;

	of_property_read_string(np, "clock-output-names", &name);
	of_property_read_u32(np, "clock-frequency", &frequency);
	of_property_read_u32(np, "clock-accuracy", &accuracy);
	of_property_read_u32(np, "atmel,startup-time-usec", &startup);

	clk = at91_clk_register_slow_rc_osc(sckcr, name, frequency, accuracy,
					    startup);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static int clk_sam9x5_slow_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_sam9x5_slow *slowck = to_clk_sam9x5_slow(hw);
	void __iomem *sckcr = slowck->sckcr;
	u32 tmp;

	if (index > 1)
		return -EINVAL;

	tmp = readl(sckcr);

	if ((!index && !(tmp & AT91_SCKC_OSCSEL)) ||
	    (index && (tmp & AT91_SCKC_OSCSEL)))
		return 0;

	if (index)
		tmp |= AT91_SCKC_OSCSEL;
	else
		tmp &= ~AT91_SCKC_OSCSEL;

	writel(tmp, sckcr);

	usleep_range(SLOWCK_SW_TIME_USEC, SLOWCK_SW_TIME_USEC + 1);

	return 0;
}

static u8 clk_sam9x5_slow_get_parent(struct clk_hw *hw)
{
	struct clk_sam9x5_slow *slowck = to_clk_sam9x5_slow(hw);

	return !!(readl(slowck->sckcr) & AT91_SCKC_OSCSEL);
}

static const struct clk_ops sam9x5_slow_ops = {
	.set_parent = clk_sam9x5_slow_set_parent,
	.get_parent = clk_sam9x5_slow_get_parent,
};

static struct clk * __init
at91_clk_register_sam9x5_slow(void __iomem *sckcr,
			      const char *name,
			      const char **parent_names,
			      int num_parents)
{
	struct clk_sam9x5_slow *slowck;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!sckcr || !name || !parent_names || !num_parents)
		return ERR_PTR(-EINVAL);

	slowck = kzalloc(sizeof(*slowck), GFP_KERNEL);
	if (!slowck)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &sam9x5_slow_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;

	slowck->hw.init = &init;
	slowck->sckcr = sckcr;
	slowck->parent = !!(readl(sckcr) & AT91_SCKC_OSCSEL);

	clk = clk_register(NULL, &slowck->hw);
	if (IS_ERR(clk))
		kfree(slowck);
	else
		slow_clk = clk;

	return clk;
}

void __init of_at91sam9x5_clk_slow_setup(struct device_node *np,
					 void __iomem *sckcr)
{
	struct clk *clk;
	const char *parent_names[2];
	int num_parents;
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

	clk = at91_clk_register_sam9x5_slow(sckcr, name, parent_names,
					    num_parents);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static u8 clk_sam9260_slow_get_parent(struct clk_hw *hw)
{
	struct clk_sam9260_slow *slowck = to_clk_sam9260_slow(hw);

	return !!(pmc_read(slowck->pmc, AT91_PMC_SR) & AT91_PMC_OSCSEL);
}

static const struct clk_ops sam9260_slow_ops = {
	.get_parent = clk_sam9260_slow_get_parent,
};

static struct clk * __init
at91_clk_register_sam9260_slow(struct at91_pmc *pmc,
			       const char *name,
			       const char **parent_names,
			       int num_parents)
{
	struct clk_sam9260_slow *slowck;
	struct clk *clk = NULL;
	struct clk_init_data init;

	if (!pmc || !name)
		return ERR_PTR(-EINVAL);

	if (!parent_names || !num_parents)
		return ERR_PTR(-EINVAL);

	slowck = kzalloc(sizeof(*slowck), GFP_KERNEL);
	if (!slowck)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &sam9260_slow_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;

	slowck->hw.init = &init;
	slowck->pmc = pmc;

	clk = clk_register(NULL, &slowck->hw);
	if (IS_ERR(clk))
		kfree(slowck);
	else
		slow_clk = clk;

	return clk;
}

void __init of_at91sam9260_clk_slow_setup(struct device_node *np,
					  struct at91_pmc *pmc)
{
	struct clk *clk;
	const char *parent_names[2];
	int num_parents;
	const char *name = np->name;
	int i;

	num_parents = of_count_phandle_with_args(np, "clocks", "#clock-cells");
	if (num_parents != 2)
		return;

	for (i = 0; i < num_parents; ++i) {
		parent_names[i] = of_clk_get_parent_name(np, i);
		if (!parent_names[i])
			return;
	}

	of_property_read_string(np, "clock-output-names", &name);

	clk = at91_clk_register_sam9260_slow(pmc, name, parent_names,
					     num_parents);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

/*
 * FIXME: All slow clk users are not properly claiming it (get + prepare +
 * enable) before using it.
 * If all users properly claiming this clock decide that they don't need it
 * anymore (or are removed), it is disabled while faulty users are still
 * requiring it, and the system hangs.
 * Prevent this clock from being disabled until all users are properly
 * requesting it.
 * Once this is done we should remove this function and the slow_clk variable.
 */
static int __init of_at91_clk_slow_retain(void)
{
	if (!slow_clk)
		return 0;

	__clk_get(slow_clk);
	clk_prepare_enable(slow_clk);

	return 0;
}
arch_initcall(of_at91_clk_slow_retain);
