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
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <asm/proc-fns.h>

#include "pmc.h"

struct at91_pmc_caps {
	u32 available_irqs;
};

struct at91_pmc {
	struct regmap *regmap;
	const struct at91_pmc_caps *caps;
};

int of_at91_get_clk_range(struct device_node *np, const char *propname,
			  struct clk_range *range)
{
	u32 min, max;
	int ret;

	ret = of_property_read_u32_index(np, propname, 0, &min);
	if (ret)
		return ret;

	ret = of_property_read_u32_index(np, propname, 1, &max);
	if (ret)
		return ret;

	if (range) {
		range->min = min;
		range->max = max;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(of_at91_get_clk_range);

static const struct at91_pmc_caps at91rm9200_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_LOCKB |
			  AT91_PMC_MCKRDY | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY | AT91_PMC_PCK2RDY |
			  AT91_PMC_PCK3RDY,
};

static const struct at91_pmc_caps at91sam9260_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_LOCKB |
			  AT91_PMC_MCKRDY | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY,
};

static const struct at91_pmc_caps at91sam9g45_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_MCKRDY |
			  AT91_PMC_LOCKU | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY,
};

static const struct at91_pmc_caps at91sam9n12_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_LOCKB |
			  AT91_PMC_MCKRDY | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY | AT91_PMC_MOSCSELS |
			  AT91_PMC_MOSCRCS | AT91_PMC_CFDEV,
};

static const struct at91_pmc_caps at91sam9x5_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_MCKRDY |
			  AT91_PMC_LOCKU | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY | AT91_PMC_MOSCSELS |
			  AT91_PMC_MOSCRCS | AT91_PMC_CFDEV,
};

static const struct at91_pmc_caps sama5d2_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_MCKRDY |
			  AT91_PMC_LOCKU | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY | AT91_PMC_PCK2RDY |
			  AT91_PMC_MOSCSELS | AT91_PMC_MOSCRCS |
			  AT91_PMC_CFDEV | AT91_PMC_GCKRDY,
};

static const struct at91_pmc_caps sama5d3_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_MCKRDY |
			  AT91_PMC_LOCKU | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY | AT91_PMC_PCK2RDY |
			  AT91_PMC_MOSCSELS | AT91_PMC_MOSCRCS |
			  AT91_PMC_CFDEV,
};

static void __init of_at91_pmc_setup(struct device_node *np,
				     const struct at91_pmc_caps *caps)
{
	struct at91_pmc *pmc;
	struct regmap *regmap;

	regmap = syscon_node_to_regmap(np);
	if (IS_ERR(regmap))
		panic("Could not retrieve syscon regmap");

	pmc = kzalloc(sizeof(*pmc), GFP_KERNEL);
	if (!pmc)
		return;

	pmc->regmap = regmap;
	pmc->caps = caps;

	regmap_write(pmc->regmap, AT91_PMC_IDR, 0xffffffff);

}

static void __init of_at91rm9200_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &at91rm9200_caps);
}
CLK_OF_DECLARE(at91rm9200_clk_pmc, "atmel,at91rm9200-pmc",
	       of_at91rm9200_pmc_setup);

static void __init of_at91sam9260_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &at91sam9260_caps);
}
CLK_OF_DECLARE(at91sam9260_clk_pmc, "atmel,at91sam9260-pmc",
	       of_at91sam9260_pmc_setup);

static void __init of_at91sam9g45_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &at91sam9g45_caps);
}
CLK_OF_DECLARE(at91sam9g45_clk_pmc, "atmel,at91sam9g45-pmc",
	       of_at91sam9g45_pmc_setup);

static void __init of_at91sam9n12_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &at91sam9n12_caps);
}
CLK_OF_DECLARE(at91sam9n12_clk_pmc, "atmel,at91sam9n12-pmc",
	       of_at91sam9n12_pmc_setup);

static void __init of_at91sam9x5_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &at91sam9x5_caps);
}
CLK_OF_DECLARE(at91sam9x5_clk_pmc, "atmel,at91sam9x5-pmc",
	       of_at91sam9x5_pmc_setup);

static void __init of_sama5d2_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &sama5d2_caps);
}
CLK_OF_DECLARE(sama5d2_clk_pmc, "atmel,sama5d2-pmc",
	       of_sama5d2_pmc_setup);

static void __init of_sama5d3_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &sama5d3_caps);
}
CLK_OF_DECLARE(sama5d3_clk_pmc, "atmel,sama5d3-pmc",
	       of_sama5d3_pmc_setup);
