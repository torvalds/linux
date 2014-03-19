/*
 * l2 cache initialization for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <asm/hardware/cache-l2x0.h>

static const struct of_device_id sirf_l2x0_ids[] __initconst = {
	{ .compatible = "sirf,prima2-pl310-cache", .data = &prima2_l2x0_aux, },
	{ .compatible = "sirf,marco-pl310-cache", .data = &marco_l2x0_aux, },
	{},
};

static int __init sirfsoc_l2x0_init(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, sirf_l2x0_ids);
	if (np)
		return l2x0_of_init(0, ~0);

	return 0;
}
early_initcall(sirfsoc_l2x0_init);
