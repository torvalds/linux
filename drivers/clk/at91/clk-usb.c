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

#include "pmc.h"

#define USB_SOURCE_MAX		2

#define SAM9X5_USB_DIV_SHIFT	8
#define SAM9X5_USB_MAX_DIV	0xf

#define RM9200_USB_DIV_SHIFT	28
#define RM9200_USB_DIV_TAB_SIZE	4

struct at91sam9x5_clk_usb {
	struct clk_hw hw;
	struct at91_pmc *pmc;
};

#define to_at91sam9x5_clk_usb(hw) \
	container_of(hw, struct at91sam9x5_clk_usb, hw)

struct at91rm9200_clk_usb {
	struct clk_hw hw;
	struct at91_pmc *pmc;
	u32 divisors[4];
};

#define to_at91rm9200_clk_usb(hw) \
	container_of(hw, struct at91rm9200_clk_usb, hw)

static unsigned long at91sam9x5_clk_usb_recalc_rate(struct clk_hw *hw,
						    unsigned long parent_rate)
{
	u32 tmp;
	u8 usbdiv;
	struct at91sam9x5_clk_usb *usb = to_at91sam9x5_clk_usb(hw);
	struct at91_pmc *pmc = usb->pmc;

	tmp = pmc_read(pmc, AT91_PMC_USB);
	usbdiv = (tmp & AT91_PMC_OHCIUSBDIV) >> SAM9X5_USB_DIV_SHIFT;

	return DIV_ROUND_CLOSEST(parent_rate, (usbdiv + 1));
}

static int at91sam9x5_clk_usb_determine_rate(struct clk_hw *hw,
					     struct clk_rate_request *req)
{
	struct clk_hw *parent;
	long best_rate = -EINVAL;
	unsigned long tmp_rate;
	int best_diff = -1;
	int tmp_diff;
	int i;

	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		int div;

		parent = clk_hw_get_parent_by_index(hw, i);
		if (!parent)
			continue;

		for (div = 1; div < SAM9X5_USB_MAX_DIV + 2; div++) {
			unsigned long tmp_parent_rate;

			tmp_parent_rate = req->rate * div;
			tmp_parent_rate = clk_hw_round_rate(parent,
							   tmp_parent_rate);
			tmp_rate = DIV_ROUND_CLOSEST(tmp_parent_rate, div);
			if (tmp_rate < req->rate)
				tmp_diff = req->rate - tmp_rate;
			else
				tmp_diff = tmp_rate - req->rate;

			if (best_diff < 0 || best_diff > tmp_diff) {
				best_rate = tmp_rate;
				best_diff = tmp_diff;
				req->best_parent_rate = tmp_parent_rate;
				req->best_parent_hw = parent;
			}

			if (!best_diff || tmp_rate < req->rate)
				break;
		}

		if (!best_diff)
			break;
	}

	if (best_rate < 0)
		return best_rate;

	req->rate = best_rate;
	return 0;
}

static int at91sam9x5_clk_usb_set_parent(struct clk_hw *hw, u8 index)
{
	u32 tmp;
	struct at91sam9x5_clk_usb *usb = to_at91sam9x5_clk_usb(hw);
	struct at91_pmc *pmc = usb->pmc;

	if (index > 1)
		return -EINVAL;
	tmp = pmc_read(pmc, AT91_PMC_USB) & ~AT91_PMC_USBS;
	if (index)
		tmp |= AT91_PMC_USBS;
	pmc_write(pmc, AT91_PMC_USB, tmp);
	return 0;
}

static u8 at91sam9x5_clk_usb_get_parent(struct clk_hw *hw)
{
	struct at91sam9x5_clk_usb *usb = to_at91sam9x5_clk_usb(hw);
	struct at91_pmc *pmc = usb->pmc;

	return pmc_read(pmc, AT91_PMC_USB) & AT91_PMC_USBS;
}

static int at91sam9x5_clk_usb_set_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long parent_rate)
{
	u32 tmp;
	struct at91sam9x5_clk_usb *usb = to_at91sam9x5_clk_usb(hw);
	struct at91_pmc *pmc = usb->pmc;
	unsigned long div;

	if (!rate)
		return -EINVAL;

	div = DIV_ROUND_CLOSEST(parent_rate, rate);
	if (div > SAM9X5_USB_MAX_DIV + 1 || !div)
		return -EINVAL;

	tmp = pmc_read(pmc, AT91_PMC_USB) & ~AT91_PMC_OHCIUSBDIV;
	tmp |= (div - 1) << SAM9X5_USB_DIV_SHIFT;
	pmc_write(pmc, AT91_PMC_USB, tmp);

	return 0;
}

static const struct clk_ops at91sam9x5_usb_ops = {
	.recalc_rate = at91sam9x5_clk_usb_recalc_rate,
	.determine_rate = at91sam9x5_clk_usb_determine_rate,
	.get_parent = at91sam9x5_clk_usb_get_parent,
	.set_parent = at91sam9x5_clk_usb_set_parent,
	.set_rate = at91sam9x5_clk_usb_set_rate,
};

static int at91sam9n12_clk_usb_enable(struct clk_hw *hw)
{
	struct at91sam9x5_clk_usb *usb = to_at91sam9x5_clk_usb(hw);
	struct at91_pmc *pmc = usb->pmc;

	pmc_write(pmc, AT91_PMC_USB,
		  pmc_read(pmc, AT91_PMC_USB) | AT91_PMC_USBS);
	return 0;
}

static void at91sam9n12_clk_usb_disable(struct clk_hw *hw)
{
	struct at91sam9x5_clk_usb *usb = to_at91sam9x5_clk_usb(hw);
	struct at91_pmc *pmc = usb->pmc;

	pmc_write(pmc, AT91_PMC_USB,
		  pmc_read(pmc, AT91_PMC_USB) & ~AT91_PMC_USBS);
}

static int at91sam9n12_clk_usb_is_enabled(struct clk_hw *hw)
{
	struct at91sam9x5_clk_usb *usb = to_at91sam9x5_clk_usb(hw);
	struct at91_pmc *pmc = usb->pmc;

	return !!(pmc_read(pmc, AT91_PMC_USB) & AT91_PMC_USBS);
}

static const struct clk_ops at91sam9n12_usb_ops = {
	.enable = at91sam9n12_clk_usb_enable,
	.disable = at91sam9n12_clk_usb_disable,
	.is_enabled = at91sam9n12_clk_usb_is_enabled,
	.recalc_rate = at91sam9x5_clk_usb_recalc_rate,
	.determine_rate = at91sam9x5_clk_usb_determine_rate,
	.set_rate = at91sam9x5_clk_usb_set_rate,
};

static struct clk * __init
at91sam9x5_clk_register_usb(struct at91_pmc *pmc, const char *name,
			    const char **parent_names, u8 num_parents)
{
	struct at91sam9x5_clk_usb *usb;
	struct clk *clk = NULL;
	struct clk_init_data init;

	usb = kzalloc(sizeof(*usb), GFP_KERNEL);
	if (!usb)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &at91sam9x5_usb_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE |
		     CLK_SET_RATE_PARENT;

	usb->hw.init = &init;
	usb->pmc = pmc;

	clk = clk_register(NULL, &usb->hw);
	if (IS_ERR(clk))
		kfree(usb);

	return clk;
}

static struct clk * __init
at91sam9n12_clk_register_usb(struct at91_pmc *pmc, const char *name,
			     const char *parent_name)
{
	struct at91sam9x5_clk_usb *usb;
	struct clk *clk = NULL;
	struct clk_init_data init;

	usb = kzalloc(sizeof(*usb), GFP_KERNEL);
	if (!usb)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &at91sam9n12_usb_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_SET_RATE_GATE | CLK_SET_RATE_PARENT;

	usb->hw.init = &init;
	usb->pmc = pmc;

	clk = clk_register(NULL, &usb->hw);
	if (IS_ERR(clk))
		kfree(usb);

	return clk;
}

static unsigned long at91rm9200_clk_usb_recalc_rate(struct clk_hw *hw,
						    unsigned long parent_rate)
{
	struct at91rm9200_clk_usb *usb = to_at91rm9200_clk_usb(hw);
	struct at91_pmc *pmc = usb->pmc;
	u32 tmp;
	u8 usbdiv;

	tmp = pmc_read(pmc, AT91_CKGR_PLLBR);
	usbdiv = (tmp & AT91_PMC_USBDIV) >> RM9200_USB_DIV_SHIFT;
	if (usb->divisors[usbdiv])
		return parent_rate / usb->divisors[usbdiv];

	return 0;
}

static long at91rm9200_clk_usb_round_rate(struct clk_hw *hw, unsigned long rate,
					  unsigned long *parent_rate)
{
	struct at91rm9200_clk_usb *usb = to_at91rm9200_clk_usb(hw);
	struct clk_hw *parent = clk_hw_get_parent(hw);
	unsigned long bestrate = 0;
	int bestdiff = -1;
	unsigned long tmprate;
	int tmpdiff;
	int i = 0;

	for (i = 0; i < RM9200_USB_DIV_TAB_SIZE; i++) {
		unsigned long tmp_parent_rate;

		if (!usb->divisors[i])
			continue;

		tmp_parent_rate = rate * usb->divisors[i];
		tmp_parent_rate = clk_hw_round_rate(parent, tmp_parent_rate);
		tmprate = DIV_ROUND_CLOSEST(tmp_parent_rate, usb->divisors[i]);
		if (tmprate < rate)
			tmpdiff = rate - tmprate;
		else
			tmpdiff = tmprate - rate;

		if (bestdiff < 0 || bestdiff > tmpdiff) {
			bestrate = tmprate;
			bestdiff = tmpdiff;
			*parent_rate = tmp_parent_rate;
		}

		if (!bestdiff)
			break;
	}

	return bestrate;
}

static int at91rm9200_clk_usb_set_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long parent_rate)
{
	u32 tmp;
	int i;
	struct at91rm9200_clk_usb *usb = to_at91rm9200_clk_usb(hw);
	struct at91_pmc *pmc = usb->pmc;
	unsigned long div;

	if (!rate)
		return -EINVAL;

	div = DIV_ROUND_CLOSEST(parent_rate, rate);

	for (i = 0; i < RM9200_USB_DIV_TAB_SIZE; i++) {
		if (usb->divisors[i] == div) {
			tmp = pmc_read(pmc, AT91_CKGR_PLLBR) &
			      ~AT91_PMC_USBDIV;
			tmp |= i << RM9200_USB_DIV_SHIFT;
			pmc_write(pmc, AT91_CKGR_PLLBR, tmp);
			return 0;
		}
	}

	return -EINVAL;
}

static const struct clk_ops at91rm9200_usb_ops = {
	.recalc_rate = at91rm9200_clk_usb_recalc_rate,
	.round_rate = at91rm9200_clk_usb_round_rate,
	.set_rate = at91rm9200_clk_usb_set_rate,
};

static struct clk * __init
at91rm9200_clk_register_usb(struct at91_pmc *pmc, const char *name,
			    const char *parent_name, const u32 *divisors)
{
	struct at91rm9200_clk_usb *usb;
	struct clk *clk = NULL;
	struct clk_init_data init;

	usb = kzalloc(sizeof(*usb), GFP_KERNEL);
	if (!usb)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &at91rm9200_usb_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_SET_RATE_PARENT;

	usb->hw.init = &init;
	usb->pmc = pmc;
	memcpy(usb->divisors, divisors, sizeof(usb->divisors));

	clk = clk_register(NULL, &usb->hw);
	if (IS_ERR(clk))
		kfree(usb);

	return clk;
}

void __init of_at91sam9x5_clk_usb_setup(struct device_node *np,
					struct at91_pmc *pmc)
{
	struct clk *clk;
	int num_parents;
	const char *parent_names[USB_SOURCE_MAX];
	const char *name = np->name;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents <= 0 || num_parents > USB_SOURCE_MAX)
		return;

	of_clk_parent_fill(np, parent_names, num_parents);

	of_property_read_string(np, "clock-output-names", &name);

	clk = at91sam9x5_clk_register_usb(pmc, name, parent_names, num_parents);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

void __init of_at91sam9n12_clk_usb_setup(struct device_node *np,
					 struct at91_pmc *pmc)
{
	struct clk *clk;
	const char *parent_name;
	const char *name = np->name;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name)
		return;

	of_property_read_string(np, "clock-output-names", &name);

	clk = at91sam9n12_clk_register_usb(pmc, name, parent_name);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

void __init of_at91rm9200_clk_usb_setup(struct device_node *np,
					struct at91_pmc *pmc)
{
	struct clk *clk;
	const char *parent_name;
	const char *name = np->name;
	u32 divisors[4] = {0, 0, 0, 0};

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name)
		return;

	of_property_read_u32_array(np, "atmel,clk-divisors", divisors, 4);
	if (!divisors[0])
		return;

	of_property_read_string(np, "clock-output-names", &name);

	clk = at91rm9200_clk_register_usb(pmc, name, parent_name, divisors);
	if (IS_ERR(clk))
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}
