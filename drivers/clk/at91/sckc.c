/*
 * drivers/clk/at91/sckc.c
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
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>

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

struct clk_sama5d4_slow_osc {
	struct clk_hw hw;
	void __iomem *sckcr;
	unsigned long startup_usec;
	bool prepared;
};

#define to_clk_sama5d4_slow_osc(hw) container_of(hw, struct clk_sama5d4_slow_osc, hw)

struct clk_slow_rc_osc {
	struct clk_hw hw;
	void __iomem *sckcr;
	unsigned long frequency;
	unsigned long accuracy;
	unsigned long startup_usec;
};

#define to_clk_slow_rc_osc(hw) container_of(hw, struct clk_slow_rc_osc, hw)

struct clk_sam9x5_slow {
	struct clk_hw hw;
	void __iomem *sckcr;
	u8 parent;
};

#define to_clk_sam9x5_slow(hw) container_of(hw, struct clk_sam9x5_slow, hw)

static int clk_slow_osc_prepare(struct clk_hw *hw)
{
	struct clk_slow_osc *osc = to_clk_slow_osc(hw);
	void __iomem *sckcr = osc->sckcr;
	u32 tmp = readl(sckcr);

	if (tmp & (AT91_SCKC_OSC32BYP | AT91_SCKC_OSC32EN))
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

static struct clk_hw * __init
at91_clk_register_slow_osc(void __iomem *sckcr,
			   const char *name,
			   const char *parent_name,
			   unsigned long startup,
			   bool bypass)
{
	struct clk_slow_osc *osc;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

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

	hw = &osc->hw;
	ret = clk_hw_register(NULL, &osc->hw);
	if (ret) {
		kfree(osc);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static void __init
of_at91sam9x5_clk_slow_osc_setup(struct device_node *np, void __iomem *sckcr)
{
	struct clk_hw *hw;
	const char *parent_name;
	const char *name = np->name;
	u32 startup;
	bool bypass;

	parent_name = of_clk_get_parent_name(np, 0);
	of_property_read_string(np, "clock-output-names", &name);
	of_property_read_u32(np, "atmel,startup-time-usec", &startup);
	bypass = of_property_read_bool(np, "atmel,osc-bypass");

	hw = at91_clk_register_slow_osc(sckcr, name, parent_name, startup,
					 bypass);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
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

static struct clk_hw * __init
at91_clk_register_slow_rc_osc(void __iomem *sckcr,
			      const char *name,
			      unsigned long frequency,
			      unsigned long accuracy,
			      unsigned long startup)
{
	struct clk_slow_rc_osc *osc;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	if (!sckcr || !name)
		return ERR_PTR(-EINVAL);

	osc = kzalloc(sizeof(*osc), GFP_KERNEL);
	if (!osc)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &slow_rc_osc_ops;
	init.parent_names = NULL;
	init.num_parents = 0;
	init.flags = CLK_IGNORE_UNUSED;

	osc->hw.init = &init;
	osc->sckcr = sckcr;
	osc->frequency = frequency;
	osc->accuracy = accuracy;
	osc->startup_usec = startup;

	hw = &osc->hw;
	ret = clk_hw_register(NULL, &osc->hw);
	if (ret) {
		kfree(osc);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static void __init
of_at91sam9x5_clk_slow_rc_osc_setup(struct device_node *np, void __iomem *sckcr)
{
	struct clk_hw *hw;
	u32 frequency = 0;
	u32 accuracy = 0;
	u32 startup = 0;
	const char *name = np->name;

	of_property_read_string(np, "clock-output-names", &name);
	of_property_read_u32(np, "clock-frequency", &frequency);
	of_property_read_u32(np, "clock-accuracy", &accuracy);
	of_property_read_u32(np, "atmel,startup-time-usec", &startup);

	hw = at91_clk_register_slow_rc_osc(sckcr, name, frequency, accuracy,
					    startup);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
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

static struct clk_hw * __init
at91_clk_register_sam9x5_slow(void __iomem *sckcr,
			      const char *name,
			      const char **parent_names,
			      int num_parents)
{
	struct clk_sam9x5_slow *slowck;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

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

	hw = &slowck->hw;
	ret = clk_hw_register(NULL, &slowck->hw);
	if (ret) {
		kfree(slowck);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static void __init
of_at91sam9x5_clk_slow_setup(struct device_node *np, void __iomem *sckcr)
{
	struct clk_hw *hw;
	const char *parent_names[2];
	unsigned int num_parents;
	const char *name = np->name;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents == 0 || num_parents > 2)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);

	of_property_read_string(np, "clock-output-names", &name);

	hw = at91_clk_register_sam9x5_slow(sckcr, name, parent_names,
					    num_parents);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}

static const struct of_device_id sckc_clk_ids[] __initconst = {
	/* Slow clock */
	{
		.compatible = "atmel,at91sam9x5-clk-slow-osc",
		.data = of_at91sam9x5_clk_slow_osc_setup,
	},
	{
		.compatible = "atmel,at91sam9x5-clk-slow-rc-osc",
		.data = of_at91sam9x5_clk_slow_rc_osc_setup,
	},
	{
		.compatible = "atmel,at91sam9x5-clk-slow",
		.data = of_at91sam9x5_clk_slow_setup,
	},
	{ /*sentinel*/ }
};

static void __init of_at91sam9x5_sckc_setup(struct device_node *np)
{
	struct device_node *childnp;
	void (*clk_setup)(struct device_node *, void __iomem *);
	const struct of_device_id *clk_id;
	void __iomem *regbase = of_iomap(np, 0);

	if (!regbase)
		return;

	for_each_child_of_node(np, childnp) {
		clk_id = of_match_node(sckc_clk_ids, childnp);
		if (!clk_id)
			continue;
		clk_setup = clk_id->data;
		clk_setup(childnp, regbase);
	}
}
CLK_OF_DECLARE(at91sam9x5_clk_sckc, "atmel,at91sam9x5-sckc",
	       of_at91sam9x5_sckc_setup);

static int clk_sama5d4_slow_osc_prepare(struct clk_hw *hw)
{
	struct clk_sama5d4_slow_osc *osc = to_clk_sama5d4_slow_osc(hw);

	if (osc->prepared)
		return 0;

	/*
	 * Assume that if it has already been selected (for example by the
	 * bootloader), enough time has aready passed.
	 */
	if ((readl(osc->sckcr) & AT91_SCKC_OSCSEL)) {
		osc->prepared = true;
		return 0;
	}

	usleep_range(osc->startup_usec, osc->startup_usec + 1);
	osc->prepared = true;

	return 0;
}

static int clk_sama5d4_slow_osc_is_prepared(struct clk_hw *hw)
{
	struct clk_sama5d4_slow_osc *osc = to_clk_sama5d4_slow_osc(hw);

	return osc->prepared;
}

static const struct clk_ops sama5d4_slow_osc_ops = {
	.prepare = clk_sama5d4_slow_osc_prepare,
	.is_prepared = clk_sama5d4_slow_osc_is_prepared,
};

static void __init of_sama5d4_sckc_setup(struct device_node *np)
{
	void __iomem *regbase = of_iomap(np, 0);
	struct clk_hw *hw;
	struct clk_sama5d4_slow_osc *osc;
	struct clk_init_data init;
	const char *xtal_name;
	const char *parent_names[2] = { "slow_rc_osc", "slow_osc" };
	bool bypass;
	int ret;

	if (!regbase)
		return;

	hw = clk_hw_register_fixed_rate_with_accuracy(NULL, parent_names[0],
						      NULL, 0, 32768,
						      250000000);
	if (IS_ERR(hw))
		return;

	xtal_name = of_clk_get_parent_name(np, 0);

	bypass = of_property_read_bool(np, "atmel,osc-bypass");

	osc = kzalloc(sizeof(*osc), GFP_KERNEL);
	if (!osc)
		return;

	init.name = parent_names[1];
	init.ops = &sama5d4_slow_osc_ops;
	init.parent_names = &xtal_name;
	init.num_parents = 1;
	init.flags = CLK_IGNORE_UNUSED;

	osc->hw.init = &init;
	osc->sckcr = regbase;
	osc->startup_usec = 1200000;

	if (bypass)
		writel((readl(regbase) | AT91_SCKC_OSC32BYP), regbase);

	hw = &osc->hw;
	ret = clk_hw_register(NULL, &osc->hw);
	if (ret) {
		kfree(osc);
		return;
	}

	hw = at91_clk_register_sam9x5_slow(regbase, "slowck", parent_names, 2);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(sama5d4_clk_sckc, "atmel,sama5d4-sckc",
	       of_sama5d4_sckc_setup);
